#include "moss/extensions/vulkan/attachments/images/image_system.hpp"
#include "moss/extensions/vulkan/attachments/images/components.hpp"
#include "moss/extensions/vulkan/attachments/images/gpu_resources.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/core/allocated_image.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace moss::extensions::vulkan {

namespace {

// Plain copy of the ECS data, so the queries are separate from Vulkan work
struct ImageConfig {
    std::string name;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    f32 scale = 1.0f;
    bool doubleBuffered = false;
};

/*
 * Optional components have no direct support in moss: run a DynamicQuery over
 * a one-element DynamicView and check for empty. Returns a copy.
 */
template<typename T, typename K>
std::optional<T> findOn(const K& key, Entity entity) {
    DynamicView one{ entity };
    auto rows = cmd::DynamicQuery<With<T>, cmd::View<Include<T>>>::init(key).atlas(one);
    if (rows.empty()) return std::nullopt;
    return std::optional<T>(std::get<0>(rows[0]));
}

std::string missingFormatFeatures(VkFormatFeatureFlags have, VkFormatFeatureFlags need) {
    struct Named { VkFormatFeatureFlags bit; const char* name; };
    static constexpr Named table[] = {
        { VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "storage image" },
        { VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "sampled image" },
        { VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "linear filtering" }
    };

    std::string out;
    for (const Named& entry : table) {
        if ((need & entry.bit) && !(have & entry.bit)) {
            if (!out.empty()) out += ", ";
            out += entry.name;
        }
    }
    return out;
}

}

void ImageSystem::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    VkDevice device = core.foundation.device;
    VmaAllocator allocator = core.allocator;

    /* ---------------------------- Gather ---------------------------- */
    std::vector<ImageConfig> configs;
    for (Entity entity : entities) {
        auto image = findOn<SimImage>(key, entity);
        if (!image) continue;

        ImageConfig config;
        config.name = image->name;
        config.format = image->format;
        if (auto scale = findOn<ImageScale>(key, entity)) config.scale = scale->scale;
        config.doubleBuffered = findOn<DoubleBuffered>(key, entity).has_value();
        configs.push_back(std::move(config));
    }

    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto samplerRows = cmd::DynamicQuery<With<
        SamplerSettings
    >, cmd::View<
        Include<SamplerSettings>
    >>::init(key).atlas(entities);
    M_WARN_IF(samplerRows.size() > 1,
        "{} entities carry SamplerSettings; ImageSystem uses the first and ignores the rest",
        samplerRows.size()
    );
    if (!samplerRows.empty()) {
        auto [settings] = samplerRows[0];
        filter = settings.filter;
        address = settings.address;
    }

    /* --------------------------- Validate --------------------------- */
    std::unordered_set<std::string> names;
    for (size_t i = 0; i < configs.size(); i++) {
        const ImageConfig& config = configs[i];

        if (config.name.empty())
            M_FATAL("SimImage #{} has an empty name", i);
        if (config.name == DRAW_IMAGE_NAME)
            M_FATAL("SimImage #{}: the name '{}' is reserved for the draw image", i, DRAW_IMAGE_NAME);
        if (!names.insert(config.name).second)
            M_FATAL("SimImage #{}: duplicate name '{}'", i, config.name);
        if (!(config.scale > 0.0f) || !std::isfinite(config.scale))
            M_FATAL("SimImage '{}': ImageScale must be greater than 0 (got {})", config.name, config.scale);

        // Storage + sampled are required, linear filtering only if the sampler uses it
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(core.foundation.physicalDevice, config.format, &props);

        VkFormatFeatureFlags needed =
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (filter == VK_FILTER_LINEAR) needed |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

        if ((props.optimalTilingFeatures & needed) != needed) {
            M_FATAL(
                "SimImage '{}': format {} is not usable on this GPU, it lacks {}. "
                "(32-bit float formats usually lack linear filtering; use a 16-bit float "
                "format or set SamplerSettings to VK_FILTER_NEAREST)",
                config.name, string_VkFormat(config.format),
                missingFormatFeatures(props.optimalTilingFeatures, needed)
            );
        }
    }

    /* ---------------------------- Create ---------------------------- */
    // Publish first, then fill in place (deletors capture handles by value anyway)
    GpuResources& gpu = cmd::Resource<GpuResources>::init(key).emplace();

    // One shared sampler for every COMBINED_IMAGE_SAMPLER binding
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = address;
    samplerInfo.addressModeV = address;
    samplerInfo.addressModeW = address;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &gpu.sampler));
    core.dqueue.push([device, sampler = gpu.sampler]() {
        vkDestroySampler(device, sampler, nullptr);
    });

    VkImageUsageFlags usages{};
    usages |= VK_IMAGE_USAGE_STORAGE_BIT;
    usages |= VK_IMAGE_USAGE_SAMPLED_BIT;
    usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    for (const ImageConfig& config : configs) {
        // Draw image extent times scale, rounded to nearest, never below 1
        VkExtent2D extent = {
            std::max<u32>(1u, u32(std::llround(f64(core.drawImage.imageExtent.width) * config.scale))),
            std::max<u32>(1u, u32(std::llround(f64(core.drawImage.imageExtent.height) * config.scale)))
        };

        GpuResources::Image image;
        image.doubleBuffered = config.doubleBuffered;

        const u32 sideCount = config.doubleBuffered ? 2 : 1;
        for (u32 side = 0; side < sideCount; side++) {
            image.sides[side] = utils::createImage(device, allocator, config.format, extent, usages);

            core.dqueue.push([device, allocator, created = image.sides[side]]() {
                vkDestroyImageView(device, created.imageView, nullptr);
                vmaDestroyImage(allocator, created.image, created.allocation);
            });
        }

        gpu.images.emplace(config.name, image);
    }
}

/*
 * First frame only: every side of every image goes UNDEFINED -> GENERAL and is
 * zeroed. Images never transition from UNDEFINED again, so sim state persists
 * across frames. PassSystem::tick starts with its own syncAll, which orders
 * these clears before the passes.
 */
void ImageSystem::tick(const Key<key::READ>& key) {
    if (m_ready) return;

    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    GpuResources& gpu = cmd::Resource<GpuResources>::init(key).get();
    VkCommandBuffer cmdBuf = core.frame.cmd;

    utils::syncAll(cmdBuf);

    VkClearColorValue zero = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    VkImageSubresourceRange range = utils::mask::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

    for (auto& entry : gpu.images) {
        GpuResources::Image& image = entry.second;
        const u32 sideCount = image.doubleBuffered ? 2 : 1;

        for (u32 side = 0; side < sideCount; side++) {
            utils::transitionImage(
                cmdBuf, image.sides[side].image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL
            );
            vkCmdClearColorImage(
                cmdBuf, image.sides[side].image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range
            );
        }
    }

    m_ready = true;
}

void ImageSystem::exit(const Key<key::WRITE>& key) {
    auto resource = cmd::Resource<GpuResources>::init(key);
    if (resource.contains()) resource.erase();
}

}
