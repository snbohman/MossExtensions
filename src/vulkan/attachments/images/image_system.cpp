#include "moss/extensions/vulkan/attachments/images/image_system.hpp"
#include "moss/extensions/vulkan/attachments/images/components.hpp"
#include "moss/extensions/vulkan/attachments/images/gpu_resources.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/core/allocated_image.hpp"
#include "moss/extensions/vulkan/core/allocated_buffer.hpp"
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
    std::optional<f32> scale;
    std::optional<VkExtent2D> extent;   // explicit ImageExtent
    VkExtent2D resolved = {};           // final size, filled in during validation
    bool doubleBuffered = false;
};

struct BufferConfig {
    std::string name;
    u64 size = 0;
    bool doubleBuffered = false;
};

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
    // copy() is the "optional component" read: a value, or nullopt
    auto images  = cmd::Query<With<SimImage>>::init(key);
    auto buffers = cmd::Query<With<SimBuffer>>::init(key);
    auto scales  = cmd::Query<With<ImageScale>>::init(key);
    auto extents = cmd::Query<With<ImageExtent>>::init(key);

    std::vector<ImageConfig> imageConfigs;
    std::vector<BufferConfig> bufferConfigs;
    for (Entity entity : entities) {
        auto image  = images.copy(entity);
        auto buffer = buffers.copy(entity);
        auto scale  = scales.copy(entity);
        auto extent = extents.copy(entity);
        const bool doubleBuffered = images.has<DoubleBuffered>(entity);

        if (image && buffer)
            M_FATAL("An entity carries both SimImage '{}' and SimBuffer '{}'; use one entity per resource",
                    image->name, buffer->name);

        if (image) {
            ImageConfig config;
            config.name = image->name;
            config.format = image->format;
            if (scale) config.scale = scale->scale;
            if (extent) config.extent = VkExtent2D{ extent->width, extent->height };
            config.doubleBuffered = doubleBuffered;
            imageConfigs.push_back(std::move(config));
        } else if (buffer) {
            M_WARN_IF(scale.has_value() || extent.has_value(),
                "SimBuffer '{}': ImageScale and ImageExtent only apply to a SimImage and are ignored",
                buffer->name);

            BufferConfig config;
            config.name = buffer->name;
            config.size = buffer->size;
            config.doubleBuffered = doubleBuffered;
            bufferConfigs.push_back(std::move(config));
        } else {
            M_WARN_IF(scale.has_value() || extent.has_value() || doubleBuffered,
                "An entity carries ImageScale, ImageExtent or DoubleBuffered without a SimImage or SimBuffer; they are ignored");
        }
    }

    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto samplerRows = cmd::Query<With<SamplerSettings>>::init(key).atlas(entities);
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
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(core.foundation.physicalDevice, &deviceProps);
    const u32 maxDimension = deviceProps.limits.maxImageDimension2D;
    const VkExtent2D drawExtent = { core.drawImage.imageExtent.width, core.drawImage.imageExtent.height };

    // Images and buffers share one namespace, because a Binding resolves by name
    std::unordered_set<std::string> names;
    auto claimName = [&names](const char* kind, size_t i, const std::string& name) {
        if (name.empty())
            M_FATAL("{} #{} has an empty name", kind, i);
        if (name == DRAW_IMAGE_NAME)
            M_FATAL("{} #{}: the name '{}' is reserved for the draw image", kind, i, DRAW_IMAGE_NAME);
        if (!names.insert(name).second)
            M_FATAL("{} #{}: duplicate name '{}' (images and buffers share one namespace)", kind, i, name);
    };

    for (size_t i = 0; i < imageConfigs.size(); i++) {
        ImageConfig& config = imageConfigs[i];
        claimName("SimImage", i, config.name);

        if (config.scale && config.extent)
            M_FATAL("SimImage '{}': ImageScale and ImageExtent cannot be combined, use one of them", config.name);

        if (config.scale) {
            const f32 scale = *config.scale;
            if (!(scale > 0.0f) || !std::isfinite(scale))
                M_FATAL("SimImage '{}': ImageScale must be greater than 0 (got {})", config.name, scale);

            // Draw image extent times scale, rounded to nearest, never below 1
            const f64 width  = std::round(f64(drawExtent.width)  * f64(scale));
            const f64 height = std::round(f64(drawExtent.height) * f64(scale));
            if (width > f64(maxDimension) || height > f64(maxDimension))
                M_FATAL("SimImage '{}': ImageScale {} gives {}x{}, the device allows at most {} per side",
                        config.name, scale, width, height, maxDimension);

            config.resolved = { std::max<u32>(1u, u32(width)), std::max<u32>(1u, u32(height)) };
        } else if (config.extent) {
            const VkExtent2D extent = *config.extent;
            if (extent.width == 0 || extent.height == 0)
                M_FATAL("SimImage '{}': ImageExtent must be at least 1x1 (got {}x{})",
                        config.name, extent.width, extent.height);
            if (extent.width > maxDimension || extent.height > maxDimension)
                M_FATAL("SimImage '{}': ImageExtent {}x{} exceeds the device limit of {} per side",
                        config.name, extent.width, extent.height, maxDimension);

            config.resolved = extent;
        } else {
            config.resolved = drawExtent;
        }

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

    for (size_t i = 0; i < bufferConfigs.size(); i++) {
        const BufferConfig& config = bufferConfigs[i];
        claimName("SimBuffer", i, config.name);

        if (config.size == 0)
            M_FATAL("SimBuffer '{}': the size must be greater than 0", config.name);
        if (config.size % 4 != 0)
            M_FATAL("SimBuffer '{}': the size must be a multiple of 4 bytes (got {})", config.name, config.size);
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

    for (const ImageConfig& config : imageConfigs) {
        GpuResources::Image image;
        image.doubleBuffered = config.doubleBuffered;

        const u32 sideCount = config.doubleBuffered ? 2 : 1;
        for (u32 side = 0; side < sideCount; side++) {
            image.sides[side] = utils::createImage(device, allocator, config.format, config.resolved, usages);

            core.dqueue.push([device, allocator, created = image.sides[side]]() {
                vkDestroyImageView(device, created.imageView, nullptr);
                vmaDestroyImage(allocator, created.image, created.allocation);
            });
        }

        gpu.images.emplace(config.name, image);
    }

    VkBufferUsageFlags bufferUsages{};
    bufferUsages |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferUsages |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferUsages |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferUsages |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;   // the first-frame fill

    for (const BufferConfig& config : bufferConfigs) {
        GpuResources::Buffer buffer;
        buffer.doubleBuffered = config.doubleBuffered;

        const u32 sideCount = config.doubleBuffered ? 2 : 1;
        for (u32 side = 0; side < sideCount; side++) {
            buffer.sides[side] = utils::createBuffer(allocator, config.size, bufferUsages);

            core.dqueue.push([allocator, created = buffer.sides[side]]() {
                vmaDestroyBuffer(allocator, created.buffer, created.allocation);
            });
        }

        gpu.buffers.emplace(config.name, buffer);
    }
}

/*
 * First frame only: every side of every image goes UNDEFINED -> GENERAL and is
 * zeroed, every side of every buffer is zero-filled. Images never transition
 * from UNDEFINED again and buffers are never refilled, so sim state persists
 * across frames. PassSystem::tick starts with its own syncAll, which orders
 * these writes before the passes.
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

    for (auto& entry : gpu.buffers) {
        GpuResources::Buffer& buffer = entry.second;
        const u32 sideCount = buffer.doubleBuffered ? 2 : 1;

        // Sizes are multiples of 4 (validated), so WHOLE_SIZE is exact
        for (u32 side = 0; side < sideCount; side++) {
            vkCmdFillBuffer(cmdBuf, buffer.sides[side].buffer, 0, VK_WHOLE_SIZE, 0);
        }
    }

    m_ready = true;
}

void ImageSystem::exit(const Key<key::WRITE>& key) {
    auto resource = cmd::Resource<GpuResources>::init(key);
    if (resource.contains()) resource.erase();
}

}
