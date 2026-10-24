#include "moss/commands/primitives.hpp"
#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/core/core.hpp"
#include "moss/extensions/vulkan/core/allocated_image.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace moss::extensions::vulkan {

namespace {

/*
 * vk-bootstrap's Result::value() is unchecked in release builds, so a failed
 * builder turns into undefined behaviour. Unwrap with a readable error instead.
 * (Your tree has its own fixed version of this helper: keep that one if it
 * differs. The error code is vk-bootstrap's own enum, not a VkResult.)
 */
template<typename T>
T unwrap(vkb::Result<T> result, const char* what) {
    if (!result) {
        M_ERROR(
            "vk-bootstrap: {} failed: {} (vk-bootstrap error code {})",
            what, result.error().message(), result.error().value()
        );

        std::abort();
    }

    return std::move(result.value());
}

/* -------------------------------------------------------------------- */
/* ------------------------- Feature struct merge --------------------- */
/* -------------------------------------------------------------------- */

/*
 * The 1.1, 1.2 and 1.3 feature structs are sType, pNext, then only VkBool32
 * fields. VkPhysicalDeviceFeatures has no header and only VkBool32 fields.
 * The merge ORs every VkBool32 from the first field through the last one and
 * stops at offsetof(last) + 4, NOT at sizeof(struct): on 64-bit there is
 * trailing padding that is not a feature (the 1.3 struct has 4 tail bytes).
 * The static_asserts below guard those assumptions.
 */
constexpr size_t F10_FIRST = offsetof(VkPhysicalDeviceFeatures, robustBufferAccess);
constexpr size_t F10_LAST  = offsetof(VkPhysicalDeviceFeatures, inheritedQueries);
constexpr size_t F11_FIRST = offsetof(VkPhysicalDeviceVulkan11Features, storageBuffer16BitAccess);
constexpr size_t F11_LAST  = offsetof(VkPhysicalDeviceVulkan11Features, shaderDrawParameters);
constexpr size_t F12_FIRST = offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge);
constexpr size_t F12_LAST  = offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId);
constexpr size_t F13_FIRST = offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess);
constexpr size_t F13_LAST  = offsetof(VkPhysicalDeviceVulkan13Features, maintenance4);

constexpr const char* FEATURE_NAMES_10[] = {
    "robustBufferAccess", "fullDrawIndexUint32", "imageCubeArray", "independentBlend",
    "geometryShader", "tessellationShader", "sampleRateShading", "dualSrcBlend", "logicOp",
    "multiDrawIndirect", "drawIndirectFirstInstance", "depthClamp", "depthBiasClamp",
    "fillModeNonSolid", "depthBounds", "wideLines", "largePoints", "alphaToOne", "multiViewport",
    "samplerAnisotropy", "textureCompressionETC2", "textureCompressionASTC_LDR",
    "textureCompressionBC", "occlusionQueryPrecise", "pipelineStatisticsQuery",
    "vertexPipelineStoresAndAtomics", "fragmentStoresAndAtomics",
    "shaderTessellationAndGeometryPointSize", "shaderImageGatherExtended",
    "shaderStorageImageExtendedFormats", "shaderStorageImageMultisample",
    "shaderStorageImageReadWithoutFormat", "shaderStorageImageWriteWithoutFormat",
    "shaderUniformBufferArrayDynamicIndexing", "shaderSampledImageArrayDynamicIndexing",
    "shaderStorageBufferArrayDynamicIndexing", "shaderStorageImageArrayDynamicIndexing",
    "shaderClipDistance", "shaderCullDistance", "shaderFloat64", "shaderInt64", "shaderInt16",
    "shaderResourceResidency", "shaderResourceMinLod", "sparseBinding", "sparseResidencyBuffer",
    "sparseResidencyImage2D", "sparseResidencyImage3D", "sparseResidency2Samples",
    "sparseResidency4Samples", "sparseResidency8Samples", "sparseResidency16Samples",
    "sparseResidencyAliased", "variableMultisampleRate", "inheritedQueries"
};

constexpr const char* FEATURE_NAMES_11[] = {
    "storageBuffer16BitAccess", "uniformAndStorageBuffer16BitAccess", "storagePushConstant16",
    "storageInputOutput16", "multiview", "multiviewGeometryShader", "multiviewTessellationShader",
    "variablePointersStorageBuffer", "variablePointers", "protectedMemory",
    "samplerYcbcrConversion", "shaderDrawParameters"
};

constexpr const char* FEATURE_NAMES_12[] = {
    "samplerMirrorClampToEdge", "drawIndirectCount", "storageBuffer8BitAccess",
    "uniformAndStorageBuffer8BitAccess", "storagePushConstant8", "shaderBufferInt64Atomics",
    "shaderSharedInt64Atomics", "shaderFloat16", "shaderInt8", "descriptorIndexing",
    "shaderInputAttachmentArrayDynamicIndexing", "shaderUniformTexelBufferArrayDynamicIndexing",
    "shaderStorageTexelBufferArrayDynamicIndexing", "shaderUniformBufferArrayNonUniformIndexing",
    "shaderSampledImageArrayNonUniformIndexing", "shaderStorageBufferArrayNonUniformIndexing",
    "shaderStorageImageArrayNonUniformIndexing", "shaderInputAttachmentArrayNonUniformIndexing",
    "shaderUniformTexelBufferArrayNonUniformIndexing",
    "shaderStorageTexelBufferArrayNonUniformIndexing",
    "descriptorBindingUniformBufferUpdateAfterBind", "descriptorBindingSampledImageUpdateAfterBind",
    "descriptorBindingStorageImageUpdateAfterBind", "descriptorBindingStorageBufferUpdateAfterBind",
    "descriptorBindingUniformTexelBufferUpdateAfterBind",
    "descriptorBindingStorageTexelBufferUpdateAfterBind",
    "descriptorBindingUpdateUnusedWhilePending", "descriptorBindingPartiallyBound",
    "descriptorBindingVariableDescriptorCount", "runtimeDescriptorArray", "samplerFilterMinmax",
    "scalarBlockLayout", "imagelessFramebuffer", "uniformBufferStandardLayout",
    "shaderSubgroupExtendedTypes", "separateDepthStencilLayouts", "hostQueryReset",
    "timelineSemaphore", "bufferDeviceAddress", "bufferDeviceAddressCaptureReplay",
    "bufferDeviceAddressMultiDevice", "vulkanMemoryModel", "vulkanMemoryModelDeviceScope",
    "vulkanMemoryModelAvailabilityVisibilityChains", "shaderOutputViewportIndex",
    "shaderOutputLayer", "subgroupBroadcastDynamicId"
};

constexpr const char* FEATURE_NAMES_13[] = {
    "robustImageAccess", "inlineUniformBlock", "descriptorBindingInlineUniformBlockUpdateAfterBind",
    "pipelineCreationCacheControl", "privateData", "shaderDemoteToHelperInvocation",
    "shaderTerminateInvocation", "subgroupSizeControl", "computeFullSubgroups", "synchronization2",
    "textureCompressionASTC_HDR", "shaderZeroInitializeWorkgroupMemory", "dynamicRendering",
    "shaderIntegerDotProduct", "maintenance4"
};

constexpr size_t featureCount(size_t first, size_t last) {
    return (last - first) / sizeof(VkBool32) + 1;
}

static_assert(F10_FIRST == 0 && sizeof(VkPhysicalDeviceFeatures) == F10_LAST + sizeof(VkBool32),
    "VkPhysicalDeviceFeatures is expected to be only VkBool32 fields");
static_assert(F11_FIRST == offsetof(VkPhysicalDeviceVulkan11Features, pNext) + sizeof(void*),
    "Vulkan11Features: first feature must directly follow pNext");
static_assert(F12_FIRST == offsetof(VkPhysicalDeviceVulkan12Features, pNext) + sizeof(void*),
    "Vulkan12Features: first feature must directly follow pNext");
static_assert(F13_FIRST == offsetof(VkPhysicalDeviceVulkan13Features, pNext) + sizeof(void*),
    "Vulkan13Features: first feature must directly follow pNext");
static_assert(sizeof(VkPhysicalDeviceVulkan11Features) - (F11_LAST + sizeof(VkBool32)) < alignof(VkPhysicalDeviceVulkan11Features),
    "Vulkan11Features: more than padding follows the last feature");
static_assert(sizeof(VkPhysicalDeviceVulkan12Features) - (F12_LAST + sizeof(VkBool32)) < alignof(VkPhysicalDeviceVulkan12Features),
    "Vulkan12Features: more than padding follows the last feature");
static_assert(sizeof(VkPhysicalDeviceVulkan13Features) - (F13_LAST + sizeof(VkBool32)) < alignof(VkPhysicalDeviceVulkan13Features),
    "Vulkan13Features: more than padding follows the last feature");
static_assert(sizeof(FEATURE_NAMES_10) / sizeof(FEATURE_NAMES_10[0]) == featureCount(F10_FIRST, F10_LAST),
    "FEATURE_NAMES_10 does not match the struct");
static_assert(sizeof(FEATURE_NAMES_11) / sizeof(FEATURE_NAMES_11[0]) == featureCount(F11_FIRST, F11_LAST),
    "FEATURE_NAMES_11 does not match the struct");
static_assert(sizeof(FEATURE_NAMES_12) / sizeof(FEATURE_NAMES_12[0]) == featureCount(F12_FIRST, F12_LAST),
    "FEATURE_NAMES_12 does not match the struct");
static_assert(sizeof(FEATURE_NAMES_13) / sizeof(FEATURE_NAMES_13[0]) == featureCount(F13_FIRST, F13_LAST),
    "FEATURE_NAMES_13 does not match the struct");

VkBool32 readBool(const void* base, size_t offset) {
    VkBool32 value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}

void writeBool(void* base, size_t offset, VkBool32 value) {
    std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof(value));
}

void orBools(void* dst, const void* src, size_t first, size_t last) {
    for (size_t offset = first; offset <= last; offset += sizeof(VkBool32)) {
        const bool on = readBool(dst, offset) || readBool(src, offset);
        writeBool(dst, offset, on ? VK_TRUE : VK_FALSE);
    }
}

bool anyBool(const void* base, size_t first, size_t last) {
    for (size_t offset = first; offset <= last; offset += sizeof(VkBool32))
        if (readBool(base, offset)) return true;
    return false;
}

// Names of every feature that `required` asks for and `available` lacks
void collectMissing(
    const void* required, const void* available,
    size_t first, size_t last, const char* const* names,
    std::vector<std::string>& out
) {
    for (size_t offset = first, i = 0; offset <= last; offset += sizeof(VkBool32), i++) {
        if (readBool(required, offset) && !readBool(available, offset))
            out.push_back(std::string("feature ") + names[i]);
    }
}

void addUnique(std::vector<std::string>& into, const std::vector<std::string>& names) {
    for (const std::string& name : names)
        if (std::find(into.begin(), into.end(), name) == into.end())
            into.push_back(name);
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) out += ", ";
        out += parts[i];
    }
    return out;
}

// Readable names for the format feature bits the extension cares about
std::string missingFormatFeatures(VkFormatFeatureFlags have, VkFormatFeatureFlags need) {
    struct Named { VkFormatFeatureFlags bit; const char* name; };
    static constexpr Named table[] = {
        { VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "storage image" },
        { VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, "colour attachment" },
        { VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "sampled image" },
        { VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "linear filtering" },
        { VK_FORMAT_FEATURE_BLIT_SRC_BIT, "blit source" }
    };

    std::vector<std::string> missing;
    for (const Named& entry : table)
        if ((need & entry.bit) && !(have & entry.bit)) missing.push_back(entry.name);
    return join(missing);
}

/* -------------------------------------------------------------------- */
/* ----------------------- Swapchain-lifetime objects ------------------ */
/* -------------------------------------------------------------------- */

// The draw image is blitted into the swapchain image, so it must support
// everything the passes and the blit do with it. The format never changes, so
// this runs once.
void checkDrawFormat(VkPhysicalDevice physicalDevice, VkFormat drawFormat) {
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, drawFormat, &formatProps);

    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    if ((formatProps.optimalTilingFeatures & needed) != needed) {
        M_FATAL(
            "Draw format {} is not usable on this GPU: it lacks {}",
            string_VkFormat(drawFormat),
            missingFormatFeatures(formatProps.optimalTilingFeatures, needed)
        );
    }
}

/*
 * Builds the swapchain, its image views, the render semaphores and the draw
 * image into `core`, and pushes their deletors to core.swapchainDqueue. The
 * swapchain handle itself is NOT in the queue: it must outlive the next
 * creation (it becomes oldSwapchain), so Core::exit and recreateSwapchain
 * destroy it explicitly. `old` may be VK_NULL_HANDLE.
 */
void buildSwapchainObjects(RenderCore& core, VkExtent2D desired, VkSwapchainKHR old) {
    VkDevice device = core.foundation.device;

    vkb::SwapchainBuilder swapchainBuilder{
        core.foundation.physicalDevice,
        device,
        core.foundation.surface
    };

    swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{
            .format = core.config.swapchainFormat,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        })
        .set_desired_present_mode(core.config.presentMode)
        .set_desired_extent(desired.width, desired.height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (old != VK_NULL_HANDLE) swapchainBuilder.set_old_swapchain(old);

    vkb::Swapchain vkbSwapchain = unwrap(swapchainBuilder.build(), "Swapchain creation");

    core.swapchain.extent = vkbSwapchain.extent;
    core.swapchain.requested = desired;
    core.swapchain.dirty = false;
    core.swapchain.swapchain = vkbSwapchain.swapchain;
    // The desired format is only a wish: vk-bootstrap may have fallen back
    core.swapchain.imageFormat = vkbSwapchain.image_format;
    core.swapchain.images = unwrap(vkbSwapchain.get_images(), "Swapchain images");
    core.swapchain.imageViews = unwrap(vkbSwapchain.get_image_views(), "Swapchain image views");

    // Image views before the swapchain they were created from
    core.swapchainDqueue.push([device, views = core.swapchain.imageViews]() {
        for (VkImageView view : views) vkDestroyImageView(device, view, nullptr);
    });

    /*
     * One renderSemaphore per swapchain image because swapchain.images.size()
     * != FRAME_OVERLAP. Indexed by acquired image index. The image count can
     * change on recreation, so these have swapchain lifetime.
     */
    VkSemaphoreCreateInfo semCreateInfo = utils::info::semaphoreCreate(0);
    core.renderSemaphores.assign(core.swapchain.images.size(), VK_NULL_HANDLE);
    for (VkSemaphore& sem : core.renderSemaphores) {
        VK_CHECK(vkCreateSemaphore(device, &semCreateInfo, nullptr, &sem));
    }
    core.swapchainDqueue.push([device, semaphores = core.renderSemaphores]() {
        for (VkSemaphore sem : semaphores) vkDestroySemaphore(device, sem, nullptr);
    });

    // The draw image follows the ACTUAL swapchain extent, which can differ
    // from the requested size (HiDPI, clamping)
    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;          // so "draw" can be sampled
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    core.drawImage = utils::createImage(
        device,
        core.allocator,
        core.config.drawFormat,
        core.swapchain.extent,
        drawImageUsages
    );

    // Everything captured by value
    core.swapchainDqueue.push([device,
                               allocator = core.allocator,
                               image = core.drawImage]() {
        vkDestroyImageView(device, image.imageView, nullptr);
        vmaDestroyImage(allocator, image.image, image.allocation);
    });
}

}

/* -------------------------------------------------------------------- */
/* ---------------------------- Requirements --------------------------- */
/* -------------------------------------------------------------------- */

struct Core::Requirements {
    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;

    VkPhysicalDeviceFeatures features10 = {};
    VkPhysicalDeviceVulkan11Features features11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
    };
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
};

Core::Requirements Core::collectRequirements(
    const Key<key::WRITE>& key,
    const DynamicView& entities
) {
    Requirements req;

    // Baseline, always on. Nothing is baseline for 1.0 and 1.1.
    req.features13.dynamicRendering = VK_TRUE;
    req.features13.synchronization2 = VK_TRUE;
    req.features12.bufferDeviceAddress = VK_TRUE;
    req.features12.descriptorIndexing = VK_TRUE;

    auto instanceRows = cmd::Query<With<RequireInstanceExtension>>::init(key).atlas(entities);
    for (auto& [ext] : instanceRows)
        addUnique(req.instanceExtensions, ext.names);

    auto deviceRows = cmd::Query<With<RequireDeviceExtension>>::init(key).atlas(entities);
    for (auto& [ext] : deviceRows)
        addUnique(req.deviceExtensions, ext.names);

    auto rows10 = cmd::Query<With<RequireFeatures10>>::init(key).atlas(entities);
    for (auto& [f] : rows10)
        orBools(&req.features10, &f.features, F10_FIRST, F10_LAST);

    auto rows11 = cmd::Query<With<RequireFeatures11>>::init(key).atlas(entities);
    for (auto& [f] : rows11)
        orBools(&req.features11, &f.features, F11_FIRST, F11_LAST);

    auto rows12 = cmd::Query<With<RequireFeatures12>>::init(key).atlas(entities);
    for (auto& [f] : rows12)
        orBools(&req.features12, &f.features, F12_FIRST, F12_LAST);

    auto rows13 = cmd::Query<With<RequireFeatures13>>::init(key).atlas(entities);
    for (auto& [f] : rows13)
        orBools(&req.features13, &f.features, F13_FIRST, F13_LAST);

    return req;
}

/*
 * vk-bootstrap reports an unsupported device extension or feature as a generic
 * "no suitable device". Ask Vulkan directly instead: for every GPU, list what
 * it lacks of what was required. Called only after selection failed.
 */
void Core::diagnoseDevices(VkInstance instance, const Requirements& req) {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    if (count == 0) {
        M_ERRORF("No suitable GPU: the Vulkan instance reports no physical devices");
        return;
    }
    M_ERROR("No suitable GPU. Checked {} GPU(s) against the requirements:", count);

    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        std::vector<std::string> missing;

        if (props.apiVersion < VK_API_VERSION_1_3) {
            missing.push_back(
                "Vulkan 1.3 (device reports " + std::to_string(VK_API_VERSION_MAJOR(props.apiVersion))
                + "." + std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + ")"
            );
        }

        u32 extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

        for (const std::string& name : req.deviceExtensions) {
            const bool found = std::any_of(available.begin(), available.end(),
                [&](const VkExtensionProperties& p) { return name == p.extensionName; });
            if (!found) missing.push_back("extension " + name);
        }

        // Query what the device supports; structs a device is too old for stay zeroed
        VkPhysicalDeviceVulkan11Features have11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        VkPhysicalDeviceVulkan12Features have12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        VkPhysicalDeviceVulkan13Features have13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceFeatures2 have2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

        if (props.apiVersion >= VK_API_VERSION_1_1) {
            void** tail = &have2.pNext;
            *tail = &have11; tail = &have11.pNext;
            if (props.apiVersion >= VK_API_VERSION_1_2) { *tail = &have12; tail = &have12.pNext; }
            if (props.apiVersion >= VK_API_VERSION_1_3) { *tail = &have13; tail = &have13.pNext; }
            vkGetPhysicalDeviceFeatures2(device, &have2);
        } else {
            vkGetPhysicalDeviceFeatures(device, &have2.features);
        }

        collectMissing(&req.features10, &have2.features, F10_FIRST, F10_LAST, FEATURE_NAMES_10, missing);
        collectMissing(&req.features11, &have11, F11_FIRST, F11_LAST, FEATURE_NAMES_11, missing);
        collectMissing(&req.features12, &have12, F12_FIRST, F12_LAST, FEATURE_NAMES_12, missing);
        collectMissing(&req.features13, &have13, F13_FIRST, F13_LAST, FEATURE_NAMES_13, missing);

        if (missing.empty()) {
            M_ERROR(
                "  GPU '{}': has every required extension and feature (it may lack "
                "presentation support for the window surface)", props.deviceName
            );
        } else {
            M_ERROR("  GPU '{}': missing {}", props.deviceName, join(missing));
        }
    }
}

/* -------------------------------------------------------------------- */
/* ------------------------------- Build ------------------------------- */
/* -------------------------------------------------------------------- */

void Core::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    auto rows = cmd::Query<With<WindowSettings, RendererSettings>>::init(key).atlas(entities);

    if (rows.empty()) {
        M_ERRORF("Core needs one entity with WindowSettings and RendererSettings in its Context");
        std::abort();
    }
    M_WARN_IF(rows.size() > 1,
        "{} entities carry WindowSettings and RendererSettings; Core uses the first and ignores the rest",
        rows.size()
    );
    auto [windowSettings, renderSettings] = rows[0];

    Requirements requirements = collectRequirements(key, entities);

    // Publish first, then initialise in place: deletors never point at a local
    RenderCore& core = cmd::Resource<RenderCore>::init(key).emplace();

    initGlfw(core, windowSettings);
    initVulkan(core, renderSettings, requirements);
    initSwapchain(core, windowSettings, renderSettings);
    initCommands(core);
    initSyncStructures(core);
}

void Core::initGlfw(RenderCore& core, WindowSettings windowSettings) {
    glfwSetErrorCallback([](int code, const char* message) {
        M_ERROR("GLFW error {}: {}", code, message);
    });

    if (!glfwInit()) {
        M_ERRORF("glfwInit failed");
        std::abort();
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!windowSettings.resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    core.foundation.window = glfwCreateWindow(
        windowSettings.width,
        windowSettings.height,
        windowSettings.title,
        nullptr, nullptr
    );

    if (core.foundation.window == nullptr) {
        M_ERRORF("glfwCreateWindow failed");
        std::abort();
    }
}

void Core::initVulkan(
    RenderCore& core,
    RendererSettings rendererSettings,
    const Requirements& requirements
) {
    vkb::InstanceBuilder builder;
    builder.set_app_name("~Moss Window~")
        .request_validation_layers(rendererSettings.validationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0);

    // vk-bootstrap stores the raw const char*: `requirements` outlives build()
    for (const std::string& name : requirements.instanceExtensions)
        builder.enable_extension(name.c_str());

    vkb::Instance vkbInst = unwrap(builder.build(), "Instance creation");

    // Grab the instance
    core.foundation.instance = vkbInst.instance;
    core.foundation.debugMessenger = vkbInst.debug_messenger;

    VK_CHECK(glfwCreateWindowSurface(
        core.foundation.instance, core.foundation.window, nullptr, &core.foundation.surface
    ));

    // Use vkbootstrap to select a gpu.
    // We want a gpu that can write to the GLFW surface and supports vulkan 1.3
    // with the baseline features plus whatever the requirement components asked for
    vkb::PhysicalDeviceSelector selector{vkbInst};
    selector
        .set_minimum_version(1, 3)
        .set_required_features_13(requirements.features13)
        .set_required_features_12(requirements.features12)
        .set_surface(core.foundation.surface);

    // Only hand over the optional structs when they actually request something
    if (anyBool(&requirements.features10, F10_FIRST, F10_LAST))
        selector.set_required_features(requirements.features10);
    if (anyBool(&requirements.features11, F11_FIRST, F11_LAST))
        selector.set_required_features_11(requirements.features11);
    for (const std::string& name : requirements.deviceExtensions)
        selector.add_required_extension(name.c_str());

    auto selected = selector.select();
    if (!selected) {
        M_ERROR(
            "vk-bootstrap: Physical device selection failed: {}",
            selected.error().message()
        );
        diagnoseDevices(core.foundation.instance, requirements);
        std::abort();
    }
    vkb::PhysicalDevice physicalDevice = std::move(selected.value());
    M_INFO("GPU: {}", physicalDevice.name);

    // Create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = unwrap(deviceBuilder.build(), "Device creation");

    // Get the VkDevice handle used in the rest of a vulkan application
    core.foundation.device           = vkbDevice.device;
    core.foundation.physicalDevice   = physicalDevice.physical_device;

    // Use vkboostrap to get a Graphics queue and family
    core.foundation.graphicsQueue = unwrap(
        vkbDevice.get_queue(vkb::QueueType::graphics), "Graphics queue"
    );
    core.foundation.queueFamily = unwrap(
        vkbDevice.get_queue_index(vkb::QueueType::graphics), "Graphics queue family"
    );

    // Initialize VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = core.foundation.physicalDevice;
    allocatorInfo.device = core.foundation.device;
    allocatorInfo.instance = core.foundation.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &core.allocator));

    // Pushed first, so it is destroyed last (queue is flushed LIFO)
    core.dqueue.push([allocator = core.allocator]() {
        vmaDestroyAllocator(allocator);
    });

    // Flag foundation as initialized
    core.foundation.initialized = true;
}

void Core::initSwapchain(
    RenderCore& core,
    WindowSettings windowSettings,
    RendererSettings rendererSettings
) {
    // Kept for every later recreation. Present mode and swapchain format are
    // DESIRED, not required; the draw format is checked once, here.
    core.config.presentMode = rendererSettings.presentMode;
    core.config.swapchainFormat = rendererSettings.swapchainFormat;
    core.config.drawFormat = rendererSettings.drawFormat;
    core.config.resizable = windowSettings.resize;
    core.config.clearDraw = rendererSettings.clearColor.has_value();
    if (core.config.clearDraw) {
        for (int i = 0; i < 4; i++)
            core.config.clearColor.float32[i] = (*rendererSettings.clearColor)[i];
    }

    checkDrawFormat(core.foundation.physicalDevice, core.config.drawFormat);

    VkExtent2D desired = { windowSettings.width, windowSettings.height };

    // A resizable window is compared with its framebuffer size every frame, so
    // start from that size (it differs from the window size on HiDPI screens)
    if (windowSettings.resize) {
        int width = 0, height = 0;
        glfwGetFramebufferSize(core.foundation.window, &width, &height);
        if (width > 0 && height > 0) desired = { u32(width), u32(height) };
    }

    buildSwapchainObjects(core, desired, VK_NULL_HANDLE);
}

void Core::initCommands(RenderCore& core) {
    // Create a command pool for commands submitted to the graphics queue.
    // We also want the pool to allow for resetting of individual command buffers
    VkCommandPoolCreateInfo cmdPoolInfo = utils::info::cmdPoolCreate(
        core.foundation.queueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );

    for (u32 i = 0; i < RenderCore::FrameData::FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(
            core.foundation.device,
            &cmdPoolInfo,
            nullptr,
            &core.frames[i].commandPool
        ));

        // Allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = utils::info::cmdBufAllocate(
            core.frames[i].commandPool,
            1
        );

        // Allocate command buffer for every frame (2 frames)
        VK_CHECK(vkAllocateCommandBuffers(
            core.foundation.device,
            &cmdAllocInfo,
            &core.frames[i].mainCommandBuffer
        ));
    }
}

void Core::initSyncStructures(RenderCore& core) {
    // One fence to control when the gpu has finished rendering the frame,
    // and a semaphore to syncronize rendering with the swapchain.
    // We want the fence to start signalled so we can wait on it on the first frame.
    // (The per swapchain image render semaphores live with the swapchain.)
    VkFenceCreateInfo fenceCreateInfo = utils::info::fenceCreate(
        VK_FENCE_CREATE_SIGNALED_BIT
    );
    VkSemaphoreCreateInfo semCreateInfo = utils::info::semaphoreCreate(0);

    for (u32 i = 0; i < RenderCore::FrameData::FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(
            core.foundation.device, &fenceCreateInfo, nullptr, &core.frames[i].renderFence
        ));
        VK_CHECK(vkCreateSemaphore(
            core.foundation.device, &semCreateInfo, nullptr, &core.frames[i].swapchainSemaphore
        ));
    }
}

/* -------------------------------------------------------------------- */
/* ------------------------------ Resize ------------------------------- */
/* -------------------------------------------------------------------- */

void recreateSwapchain(RenderCore& core, VkExtent2D desired) {
    VkDevice device = core.foundation.device;

    // Nothing may still use the old views, semaphores or draw image
    VK_CHECK(vkDeviceWaitIdle(device));

    VkSwapchainKHR old = core.swapchain.swapchain;
    core.swapchainDqueue.flush();

    buildSwapchainObjects(core, desired, old);
    vkDestroySwapchainKHR(device, old, nullptr);

    core.swapchainGeneration++;
    M_DEB(
        "Swapchain recreated: {}x{} (generation {})",
        core.swapchain.extent.width, core.swapchain.extent.height, core.swapchainGeneration
    );
}

/*
 * Teardown. Attachments need no Vulkan teardown in exit(): everything they
 * create has a deletor in core.dqueue, which is flushed here in reverse order
 * of creation. Core is connected first, so moss runs its exit last, after every
 * attachment's own exit.
 */
void Core::exit(const Key<key::WRITE>& key) {
    auto resource = cmd::Resource<RenderCore>::init(key);
    RenderCore* found = resource.find();
    if (found == nullptr || !found->foundation.initialized) return;

    RenderCore& core = *found;
    VkDevice device = core.foundation.device;

    // Wait for it to become quiet
    vkDeviceWaitIdle(device);

    // Per frame deletors, then per frame command pool and sync objects
    for (u32 i = 0; i < RenderCore::FrameData::FRAME_OVERLAP; i++) {
        core.frames[i].dqueue.flush();

        vkDestroyCommandPool(device, core.frames[i].commandPool, nullptr);
        vkDestroyFence(device, core.frames[i].renderFence, nullptr);
        vkDestroySemaphore(device, core.frames[i].swapchainSemaphore, nullptr);
    }

    // Swapchain lifetime: image views, render semaphores, draw image (it needs
    // the allocator, so this comes before dqueue), then the swapchain itself
    core.swapchainDqueue.flush();
    vkDestroySwapchainKHR(device, core.swapchain.swapchain, nullptr);

    // App lifetime deletors: attachments first, allocator last
    core.dqueue.flush();

    // Surface and device
    vkDestroySurfaceKHR(core.foundation.instance, core.foundation.surface, nullptr);
    vkDestroyDevice(device, nullptr);

    // Messenger, instance
    vkb::destroy_debug_utils_messenger(core.foundation.instance, core.foundation.debugMessenger);
    vkDestroyInstance(core.foundation.instance, nullptr);

    // GLFW
    glfwDestroyWindow(core.foundation.window);
    glfwTerminate();

    resource.erase();
}

}
