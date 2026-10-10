#include "moss/commands/primitives.hpp"
#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/renderer.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace moss::extensions::vulkan {

void Renderer::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    auto [windowSettings, renderSettings] = cmd::DynamicQuery<With<
        WindowSettings, RendererSettings
    >, cmd::View<
        Include<WindowSettings, RendererSettings>
    >>::init(key).atlas(entities)[0];

    // Copy everything we need out of the ECS before touching Vulkan
    std::vector<ImageConfig> imageConfigs = gatherImages(key, entities);
    std::vector<PassConfig> passConfigs = gatherPasses(key, entities);

    initGlfw(windowSettings);
    initVulkan(renderSettings);
    initSwapchain(windowSettings);
    initCommands();
    initSyncStructures();
    initImages(imageConfigs);
    initDescriptors(passConfigs);   // layouts + sets (one per pass, two if ping-pong)
    initPipelines(passConfigs);     // needs the layouts from initDescriptors
}

void Renderer::tick(const Key<key::READ>& key) {
    if (glfwWindowShouldClose(m_foundation.window)) {
        moss::cmd::Quit::init(key).quit();
    }

    glfwPollEvents();
    draw();
}

/* -------------------------------------------------------------------- */
/* --------------------------- ECS gathering -------------------------- */
/* -------------------------------------------------------------------- */

std::vector<Renderer::ImageConfig> Renderer::gatherImages(
    const Key<key::WRITE>& key,
    const DynamicView& entities
) {
    std::vector<ImageConfig> images;

    auto rows = cmd::DynamicQuery<With<
        SimImage
    >, cmd::View<
        Include<SimImage>
    >>::init(key).atlas(entities);

    for (size_t i = 0; i < rows.size(); i++) {
        auto [image] = rows[i];
        images.push_back(ImageConfig{ image.name, image.format });
    }

    return images;
}

std::vector<Renderer::PassConfig> Renderer::gatherPasses(
    const Key<key::WRITE>& key,
    const DynamicView& entities
) {
    std::vector<PassConfig> passes;

    // Compute passes
    auto computeRows = cmd::DynamicQuery<With<
        PassOrder, ComputeShader, GridDispatch, Bindings
    >, cmd::View<
        Include<
            Pass, Shader<PipelineKind::Compute>,
            PassOrder, ComputeShader, GridDispatch, Bindings
        >
    >>::init(key).atlas(entities);

    for (size_t i = 0; i < computeRows.size(); i++) {
        auto [order, shader, grid, bindings] = computeRows[i];

        PassConfig config;
        config.order = order.order;
        config.kind = PipelineKind::Compute;
        config.computePath = shader.path;
        config.groupX = grid.x;
        config.groupY = grid.y;
        config.groupZ = grid.z;
        config.layout = bindings.bindings;
        config.resources = bindings.resources;
        passes.push_back(std::move(config));
    }

    // Graphics (display) passes
    auto graphicsRows = cmd::DynamicQuery<With<
        PassOrder, VertexShader, FragmentShader, PipelineState, DrawCall, Bindings
    >, cmd::View<
        Include<
            Pass, Shader<PipelineKind::Graphics>,
            PassOrder, VertexShader, FragmentShader, PipelineState, DrawCall, Bindings
        >
    >>::init(key).atlas(entities);

    for (size_t i = 0; i < graphicsRows.size(); i++) {
        auto [order, vertex, fragment, state, drawCall, bindings] = graphicsRows[i];

        PassConfig config;
        config.order = order.order;
        config.kind = PipelineKind::Graphics;
        config.vertexPath = vertex.path;
        config.fragmentPath = fragment.path;
        config.topology = state.topology;
        config.cullMode = state.cullMode;
        config.frontFace = state.frontFace;
        config.blendEnable = state.blendEnable;
        config.vertexCount = drawCall.vertexCount;
        config.instanceCount = drawCall.instanceCount;
        config.layout = bindings.bindings;
        config.resources = bindings.resources;
        passes.push_back(std::move(config));
    }

    // Linear ordering. PassOrder doubles as the join key below, so it must be unique.
    std::sort(passes.begin(), passes.end(), [](const PassConfig& a, const PassConfig& b) {
        return a.order < b.order;
    });
    for (size_t i = 1; i < passes.size(); i++) {
        if (passes[i].order == passes[i - 1].order)
            throw std::runtime_error(
                "Two passes share PassOrder " + std::to_string(passes[i].order)
            );
    }

    // OPTIONAL PassRepeat, joined on PassOrder
    auto repeatRows = cmd::DynamicQuery<With<
        PassOrder, PassRepeat
    >, cmd::View<
        Include<Pass, PassOrder, PassRepeat>
    >>::init(key).atlas(entities);

    for (size_t i = 0; i < repeatRows.size(); i++) {
        auto [order, repeat] = repeatRows[i];
        for (PassConfig& pass : passes)
            if (pass.order == order.order) pass.repeat = repeat.count;
    }

    // OPTIONAL PingPong, joined on PassOrder
    auto pingPongRows = cmd::DynamicQuery<With<
        PassOrder, PingPong
    >, cmd::View<
        Include<Pass, PassOrder, PingPong>
    >>::init(key).atlas(entities);

    for (size_t i = 0; i < pingPongRows.size(); i++) {
        auto [order, pingPong] = pingPongRows[i];
        for (PassConfig& pass : passes) {
            if (pass.order != order.order) continue;
            if (pingPong.a == pingPong.b)
                throw std::runtime_error("PingPong needs two different images");
            pass.pingPong = true;
            pass.pingA = pingPong.a;
            pass.pingB = pingPong.b;
        }
    }

    return passes;
}

/* -------------------------------------------------------------------- */
/* ------------------------------- Init ------------------------------- */
/* -------------------------------------------------------------------- */

void Renderer::initGlfw(WindowSettings windowSettings) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!windowSettings.resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    m_foundation.window = glfwCreateWindow(
        windowSettings.width,
        windowSettings.height,
        windowSettings.title,
        nullptr, nullptr
    );
}

void Renderer::initVulkan(RendererSettings rendererSettings) {
    vkb::InstanceBuilder builder;
    vkb::Instance vkbInst = builder.set_app_name("~Moss Window~")
        .request_validation_layers(rendererSettings.validationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build()
        .value();

    // Grab the instance 
    m_foundation.instance = vkbInst.instance;
    m_foundation.debugMessenger = vkbInst.debug_messenger;

    VkResult err = glfwCreateWindowSurface(
        m_foundation.instance, m_foundation.window, nullptr, &m_foundation.surface
    );

    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    // Vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    // Use vkbootstrap to select a gpu.
    // We want a gpu that can write to the GLFW surface and supports vulkan 1.3
    // with the correct features
    vkb::PhysicalDeviceSelector selector{vkbInst};
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(m_foundation.surface)
        .select()
        .value();

    // Create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    m_foundation.device           = vkbDevice.device;
    m_foundation.physicalDevice   = physicalDevice.physical_device;

    // Use vkboostrap to get a Graphics queue and family
    m_foundation.graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_foundation.queueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // Initialize VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_foundation.physicalDevice;
    allocatorInfo.device = m_foundation.device;
    allocatorInfo.instance = m_foundation.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &m_allocator);

    m_dqueue.push([&]() {
        vmaDestroyAllocator(m_allocator);
    });

    // Flag foundation as initialized
    m_foundation.initialized = true;
}

void Renderer::initSwapchain(WindowSettings windowSettings) {
	m_swapchain.imageFormat  = VK_FORMAT_B8G8R8A8_UNORM;
	vkb::SwapchainBuilder swapchainBuilder{
        m_foundation.physicalDevice,
        m_foundation.device,
        m_foundation.surface
    };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{
            .format = m_swapchain.imageFormat,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(windowSettings.width, windowSettings.height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	m_swapchain.extent = vkbSwapchain.extent;
	m_swapchain.swapchain = vkbSwapchain.swapchain;
	m_swapchain.images = vkbSwapchain.get_images().value();
	m_swapchain.imageViews = vkbSwapchain.get_image_views().value();

	// Draw image size will match the window
	VkExtent3D drawImageExtent = {
		windowSettings.width,
		windowSettings.height,
		1
	};

	// Hardcoding the draw format to 16 bit float per channel
	m_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	m_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimgInfo = utils::info::imageCreate(
        m_drawImage.imageFormat,
        drawImageUsages,
        drawImageExtent
    );

	// For the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimgAllocInfo = {};
	rimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(
        m_allocator,
        &rimgInfo,
        &rimgAllocInfo,
        &m_drawImage.image,
        &m_drawImage.allocation,
        nullptr
    );

	// Build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = utils::info::imageViewCreate(
        m_drawImage.imageFormat,
        m_drawImage.image,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

	VK_CHECK(vkCreateImageView(
        m_foundation.device,
        &rview_info,
        nullptr,
        &m_drawImage.imageView
    ));

	// Add to deletion queues
	m_dqueue.push([=, this]() {
		vkDestroyImageView(m_foundation.device, m_drawImage.imageView, nullptr);
		vmaDestroyImage(m_allocator, m_drawImage.image, m_drawImage.allocation);
	});
}

void Renderer::initCommands() {
	// Create a command pool for commands submitted to the graphics queue.
	// We also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo cmdPoolInfo = utils::info::cmdPoolCreate(
        m_foundation.queueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );
	
	for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(
            m_foundation.device,
            &cmdPoolInfo,
            nullptr,
            &m_frames[i].commandPool
        ));

		// Allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = utils::info::cmdBufAllocate(
            m_frames[i].commandPool,
            1
        );

        // Allocate command buffer for every frame (2 frames)
        VK_CHECK(vkAllocateCommandBuffers(
            m_foundation.device,
            &cmdAllocInfo,
            &m_frames[i].mainCommandBuffer
        ));
	}
}

void Renderer::initSyncStructures() {
	// Create syncronization structures
	// One fence to control when the gpu has finished rendering the frame,
	// and 2 semaphores to syncronize rendering with swapchain
	// We want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = utils::info::fenceCreate(
        VK_FENCE_CREATE_SIGNALED_BIT
    );
	VkSemaphoreCreateInfo semCreateInfo = utils::info::semaphoreCreate(0);

	for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(
            m_foundation.device, &fenceCreateInfo, nullptr, &m_frames[i].renderFence
        )); VK_CHECK(vkCreateSemaphore(
            m_foundation.device, &semCreateInfo, nullptr, &m_frames[i].swapchainSemaphore
        ));
	}

    /*
     * Create a seperate renderSemaphore becuase swapchain.images.size() !=
     * FRAME_OVERLAP
     */
    m_renderSemaphore.resize(m_swapchain.images.size());
    for (auto& sem : m_renderSemaphore) {
        VK_CHECK(vkCreateSemaphore(m_foundation.device, &semCreateInfo, nullptr, &sem));
    }
}

/*
 * One image per SimImage entity (density, velocity, pressure, ...). Same
 * generic AllocatedImage shape as the draw image, sized to match it.
 * Storage + sampled so a pass can write it as a storage image and the display
 * pass can sample it. Layout transition + zero-clear happens once, in
 * drawPasses(), because the sim state must survive from frame to frame.
 */
void Renderer::initImages(const std::vector<ImageConfig>& configs) {
    for (const ImageConfig& config : configs) {
        if (config.name.empty() || config.name == DRAW_IMAGE_NAME)
            throw std::runtime_error("SimImage name '" + config.name + "' is reserved/empty");
        if (m_images.count(config.name))
            throw std::runtime_error("Duplicate SimImage name '" + config.name + "'");

        AllocatedImage image = {};
        image.imageFormat = config.format;
        image.imageExtent = m_drawImage.imageExtent;
        image.drawExtent = VkExtent2D{ image.imageExtent.width, image.imageExtent.height };

        VkImageUsageFlags usages{};
        usages |= VK_IMAGE_USAGE_STORAGE_BIT;
        usages |= VK_IMAGE_USAGE_SAMPLED_BIT;
        usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VkImageCreateInfo imageInfo = utils::info::imageCreate(
            image.imageFormat, usages, image.imageExtent
        );

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vmaCreateImage(
            m_allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr
        ));

        VkImageViewCreateInfo viewInfo = utils::info::imageViewCreate(
            image.imageFormat, image.image, VK_IMAGE_ASPECT_COLOR_BIT
        );
        VK_CHECK(vkCreateImageView(
            m_foundation.device, &viewInfo, nullptr, &image.imageView
        ));

        m_images.emplace(config.name, image);

        m_dqueue.push([this, image]() {
            vkDestroyImageView(m_foundation.device, image.imageView, nullptr);
            vmaDestroyImage(m_allocator, image.image, image.allocation);
        });
    }
}

/*
 * Builds, for EVERY pass entity, its own descriptor set layout + descriptor
 * set (two sets when the pass ping-pongs). Pipelines are built afterwards in
 * initPipelines() from the layouts created here. m_passes ends up in the same
 * (PassOrder-sorted) order as `configs`.
 */
void Renderer::initDescriptors(const std::vector<PassConfig>& configs) {
    // Shared sampler for COMBINED_IMAGE_SAMPLER bindings
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(
        m_foundation.device, &samplerInfo, nullptr, &m_descriptors.sampler
    ));
    m_dqueue.push([this]() {
        vkDestroySampler(m_foundation.device, m_descriptors.sampler, nullptr);
    });

    if (configs.empty()) return; // nothing to draw; no pool needed

    // Size the pool: total sets, and per descriptor type the largest count any
    // single set needs (integral "ratios", so ratio * maxSets is exact).
    u32 totalSets = 0;
    std::map<VkDescriptorType, u32> maxPerSet;
    for (const PassConfig& config : configs) {
        totalSets += config.pingPong ? 2 : 1;

        std::map<VkDescriptorType, u32> perSet;
        for (const VkDescriptorSetLayoutBinding& b : config.layout)
            perSet[b.descriptorType] += b.descriptorCount;
        for (const auto& [type, count] : perSet)
            maxPerSet[type] = std::max(maxPerSet[type], count);
    }

    std::vector<utils::interface::DescriptorAllocator::PoolSizeRatio> sizes;
    for (const auto& [type, count] : maxPerSet)
        sizes.push_back({ type, float(count) });
    if (sizes.empty()) // passes with no bindings at all; pools may not be empty
        sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f });

    m_descriptors.allocator.init(m_foundation.device, totalSets, sizes);
    m_dqueue.push([this]() {
        m_descriptors.allocator.destroy(m_foundation.device);
    });

    for (const PassConfig& config : configs) {
        CompiledPass pass = {};
        pass.order = config.order;
        pass.kind = config.kind;
        pass.repeat = config.repeat;
        pass.groupX = config.groupX;
        pass.groupY = config.groupY;
        pass.groupZ = config.groupZ;
        pass.vertexCount = config.vertexCount;
        pass.instanceCount = config.instanceCount;
        pass.setCount = config.pingPong ? 2 : 1;

        VkShaderStageFlags stages = (config.kind == PipelineKind::Compute)
            ? VK_SHADER_STAGE_COMPUTE_BIT
            : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

        pass.setLayout = utils::createDescriptorSetLayout(
            m_foundation.device, config.layout, stages
        );
        m_dqueue.push([this, layout = pass.setLayout]() {
            vkDestroyDescriptorSetLayout(m_foundation.device, layout, nullptr);
        });

        // Set 0 = bindings as declared. Set 1 (ping-pong only) = names swapped.
        std::unordered_set<std::string> touched;
        for (u32 s = 0; s < pass.setCount; s++) {
            pass.sets[s] = m_descriptors.allocator.allocate(m_foundation.device, pass.setLayout);
            writeDescriptorSet(pass.sets[s], config, s == 1);

            for (const Binding& b : config.resources)
                touched.insert(bindingKey(config, b, s == 1));
        }
        pass.touched.assign(touched.begin(), touched.end());

        // Stages/accesses used when this pass is the source/destination of a barrier
        if (config.kind == PipelineKind::Compute) {
            pass.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            pass.writeAccess = VK_ACCESS_2_SHADER_WRITE_BIT;
            pass.readWriteAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        } else {
            pass.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                       | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                       | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            pass.writeAccess = VK_ACCESS_2_SHADER_WRITE_BIT
                             | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            pass.readWriteAccess = VK_ACCESS_2_SHADER_READ_BIT
                                 | VK_ACCESS_2_SHADER_WRITE_BIT
                                 | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                                 | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        m_passes.push_back(std::move(pass));
    }
}

void Renderer::initPipelines(const std::vector<PassConfig>& configs) {
    utils::interface::PipelineBuilder builder(m_foundation.device);

    for (size_t i = 0; i < configs.size(); i++) {
        const PassConfig& config = configs[i];
        CompiledPass& pass = m_passes[i];

        utils::interface::BuiltPipeline built;
        if (config.kind == PipelineKind::Compute) {
            built = builder.buildCompute(config.computePath, pass.setLayout);
        } else {
            PipelineState state(
                config.topology, config.cullMode, config.frontFace, config.blendEnable
            );
            // Graphics passes always render into the draw image
            built = builder.buildGraphics(
                config.vertexPath, config.fragmentPath, pass.setLayout,
                state, m_drawImage.imageFormat
            );
        }

        pass.pipeline = built.pipeline;
        pass.layout = built.layout;

        m_dqueue.push([this, built]() {
            vkDestroyPipeline(m_foundation.device, built.pipeline, nullptr);
            vkDestroyPipelineLayout(m_foundation.device, built.layout, nullptr);
        });
    }
}

/* -------------------------------------------------------------------- */
/* ----------------------- Descriptor-write helpers ------------------- */
/* -------------------------------------------------------------------- */

/*
 * Resolves a binding to a unique resource key. Image bindings resolve to an
 * image name ("" -> "draw"); on the swapped ping-pong set the pair's two names
 * trade places. Used for descriptor writes AND for barrier hazard tracking.
 */
std::string Renderer::bindingKey(
    const PassConfig& config,
    const Binding& binding,
    bool swapped
) const {
    if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
        binding.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        return "buffer:" + std::to_string((uint64_t)binding.buffer);
    }

    std::string name = binding.resource.empty() ? std::string(DRAW_IMAGE_NAME) : binding.resource;
    if (swapped && config.pingPong) {
        if (name == config.pingA) name = config.pingB;
        else if (name == config.pingB) name = config.pingA;
    }
    return name;
}

const Renderer::AllocatedImage& Renderer::resolveImage(const std::string& key) const {
    if (key == DRAW_IMAGE_NAME) return m_drawImage;

    auto it = m_images.find(key);
    if (it == m_images.end())
        throw std::runtime_error(
            "Binding references image '" + key + "' but no SimImage entity has that name"
        );
    return it->second;
}

void Renderer::writeDescriptorSet(VkDescriptorSet set, const PassConfig& config, bool swapped) {
    // reserve() so the pointers handed to the writes below never get invalidated
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    imageInfos.reserve(config.resources.size());
    bufferInfos.reserve(config.resources.size());

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(config.resources.size());

    for (const Binding& binding : config.resources) {
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = set;
        write.dstBinding = binding.index;
        write.descriptorCount = 1;
        write.descriptorType = binding.type;

        switch (binding.type) {
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            const AllocatedImage& image = resolveImage(bindingKey(config, binding, swapped));

            VkDescriptorImageInfo info = {};
            info.imageView = image.imageView;
            info.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // images stay in GENERAL during passes
            info.sampler = (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                ? m_descriptors.sampler
                : VK_NULL_HANDLE;

            imageInfos.push_back(info);
            write.pImageInfo = &imageInfos.back();
            break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
            if (binding.buffer == VK_NULL_HANDLE)
                throw std::runtime_error("Buffer binding has no VkBuffer");

            VkDescriptorBufferInfo info = {};
            info.buffer = binding.buffer;
            info.offset = binding.offset;
            info.range = binding.range;

            bufferInfos.push_back(info);
            write.pBufferInfo = &bufferInfos.back();
            break;
        }
        default:
            throw std::runtime_error("Unsupported descriptor type in Binding");
        }

        writes.push_back(write);
    }

    vkUpdateDescriptorSets(
        m_foundation.device, (u32)writes.size(), writes.data(), 0, nullptr
    );
}

/* -------------------------------------------------------------------- */
/* ------------------------------- Draw ------------------------------- */
/* -------------------------------------------------------------------- */

void Renderer::draw() {
	// Wait until the gpu has finished rendering the last frame.
    // Timeout in nanoseconds :O
    VK_CHECK(vkWaitForFences(
        m_foundation.device, 1, &getCurrentFrame().renderFence, true, 1000000000
    ));

    // Flush objects in per frame dqueue
    getCurrentFrame().dqueue.flush();

    VK_CHECK(vkResetFences(
        m_foundation.device, 1, &getCurrentFrame().renderFence
    ));

    // Requesting image from the swapchain
	u32 swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(
        m_foundation.device,
        m_swapchain.swapchain,
        1000000000,
        getCurrentFrame().swapchainSemaphore,
        nullptr,
        &swapchainImageIndex
    ));

    // Resetting current frame mainCommandBuffer after waiting for semaphores and fences
    VkCommandBuffer cmd = getCurrentFrame().mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    // Mapping the drawing dimensions to the drawImageExtent.
	m_drawImage.drawExtent.width  = m_drawImage.imageExtent.width;
	m_drawImage.drawExtent.height = m_drawImage.imageExtent.height;

    VkCommandBufferBeginInfo cmdBeginInfo = utils::info::cmdBufBegin(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    );

    // Begin Command Sequence To Buffer (simply a BeginDraw())
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));	

	// Transition our main draw image into general layout so we can write into it
    utils::transitionImage(
        cmd, m_drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL
    );

    // Run every pass entity, in PassOrder (replaces drawBackground)
	drawPasses(cmd);

	// Transition the draw image and the swapchain image into their correct transfer layouts
	utils::transitionImage(
        cmd, m_drawImage.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
	utils::transitionImage(
        cmd, m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

	// Execute a copy from the draw image into the swapchain
    utils::copyImage(
        cmd, m_drawImage.image, m_swapchain.images[swapchainImageIndex],
        m_drawImage.drawExtent, m_swapchain.extent
    );

	// Set swapchain image layout to present so we can show it on the screen
    utils::transitionImage(
        cmd, m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

	// Finalize the command buffer
	VK_CHECK(vkEndCommandBuffer(cmd));

    // Prepare the submission to the queue.
    // we want to wait on the presentSemaphore, as that semaphore is
    // signaled when the swapchain is ready we will signal the
    // renderSemaphore, to signal that rendering has finished
    VkCommandBufferSubmitInfo cmdinfo = utils::info::cmdBufSubmit(cmd);	
	
	VkSemaphoreSubmitInfo waitInfo = utils::info::semaphoreSubmit(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        getCurrentFrame().swapchainSemaphore
    );

	VkSemaphoreSubmitInfo signalInfo = utils::info::semaphoreSubmit(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        m_renderSemaphore[swapchainImageIndex] // Replaced getCurrentFrame().rS
    );	
	
	VkSubmitInfo2 submit = utils::info::submit(&cmdinfo,&signalInfo,&waitInfo);	

	// Submit command buffer to the queue and execute it.
	// renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(
        m_foundation.graphicsQueue,
        1,
        &submit,
        getCurrentFrame().renderFence
    ));

    // Prepare present: This will put the image we just rendered to into the
    // visible window. We want to wait on the renderSemaphore for that, as
    // its necessary that drawing commands have finished before the image is
    // displayed to the user.
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &m_swapchain.swapchain;
	presentInfo.swapchainCount = 1;

    // Replaced getCurrentFrame().rS
	presentInfo.pWaitSemaphores = &m_renderSemaphore[swapchainImageIndex];
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(m_foundation.graphicsQueue, &presentInfo));

	// Tick the frame number as a final last step for the draw function
	m_update.frameNumber++;
}

/*
 * Runs every CompiledPass in order, repeating each PassRepeat times.
 *
 * Everything here reads/writes m_drawImage or SimImages while they are in
 * GENERAL (draw() puts the draw image there before calling us; sim images are
 * put there once below). Graphics passes render INTO m_drawImage with dynamic
 * rendering, also in GENERAL.
 *
 * Barriers (linear, no frame graph): we remember which resources have been
 * touched since the last barrier. When a pass touches one of them again
 * (including a repeat of itself), we emit one memory barrier covering
 * everything pending, then carry on.
 */
void Renderer::drawPasses(VkCommandBuffer cmd) {
    // One-time: sim images -> GENERAL and zeroed. Never transitioned from
    // UNDEFINED again, so their contents persist across frames.
    if (!m_simImagesReady) {
        VkClearColorValue zero = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        VkImageSubresourceRange range = utils::mask::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

        for (auto& entry : m_images) {
            utils::transitionImage(
                cmd, entry.second.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL
            );
            vkCmdClearColorImage(
                cmd, entry.second.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range
            );
        }
        m_simImagesReady = true;
    }

    // Covers the previous frame's passes (sim state is read again this frame)
    // and the one-time init above.
    utils::memoryBarrier(
        cmd,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
    );

    std::unordered_set<std::string> pending;
    VkPipelineStageFlags2 pendingStages = 0;
    VkAccessFlags2 pendingWrites = 0;

    for (const CompiledPass& pass : m_passes) {
        for (u32 i = 0; i < pass.repeat; i++) {
            bool hazard = false;
            for (const std::string& key : pass.touched) {
                if (pending.count(key)) { hazard = true; break; }
            }

            if (hazard) {
                utils::memoryBarrier(
                    cmd,
                    pendingStages, pendingWrites,
                    pass.stage, pass.readWriteAccess
                );
                pending.clear();
                pendingStages = 0;
                pendingWrites = 0;
            }

            pending.insert(pass.touched.begin(), pass.touched.end());
            pendingStages |= pass.stage;
            pendingWrites |= pass.writeAccess;

            // Ping-pong passes alternate descriptor sets per iteration
            VkDescriptorSet set = pass.sets[pass.setCount > 1 ? (i % 2) : 0];

            if (pass.kind == PipelineKind::Compute) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pass.pipeline);
                vkCmdBindDescriptorSets(
                    cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pass.layout, 0, 1, &set, 0, nullptr
                );
                vkCmdDispatch(cmd, pass.groupX, pass.groupY, pass.groupZ);
            } else {
                utils::beginRendering(
                    cmd, m_drawImage.imageView, VK_IMAGE_LAYOUT_GENERAL, m_drawImage.drawExtent
                );
                utils::setViewportScissor(cmd, m_drawImage.drawExtent);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline);
                vkCmdBindDescriptorSets(
                    cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.layout, 0, 1, &set, 0, nullptr
                );
                vkCmdDraw(cmd, pass.vertexCount, pass.instanceCount, 0, 0);
                vkCmdEndRendering(cmd);
            }
        }
    }
}

void Renderer::cleanup() {
	if (m_foundation.initialized) {
        // Wait for it to become quite
		vkDeviceWaitIdle(m_foundation.device);

        // Per frame Command Pool and per frame sync objects
		for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
			vkDestroyCommandPool(m_foundation.device, m_frames[i].commandPool, nullptr);
            vkDestroyFence(m_foundation.device, m_frames[i].renderFence, nullptr);
            vkDestroySemaphore(m_foundation.device, m_frames[i].swapchainSemaphore, nullptr);

            m_frames[i].dqueue.flush(); // flush per frame data
        }

        m_dqueue.flush(); // flush game data (pipelines, layouts, pool, sampler, images)

        // Destroy seperatly allocated per swapchain image render semaphore
        for (auto& sem : m_renderSemaphore)
            vkDestroySemaphore(m_foundation.device, sem, nullptr);

        // Swapchain and image views
        vkDestroySwapchainKHR(m_foundation.device, m_swapchain.swapchain, nullptr);
        for (int i = 0; i < m_swapchain.imageViews.size(); i++)
            vkDestroyImageView(m_foundation.device, m_swapchain.imageViews[i], nullptr);

        // Surface and device
		vkDestroySurfaceKHR(m_foundation.instance, m_foundation.surface, nullptr);
		vkDestroyDevice(m_foundation.device, nullptr);
		
        // Messengar, instance
		vkb::destroy_debug_utils_messenger(m_foundation.instance, m_foundation.debugMessenger);
		vkDestroyInstance(m_foundation.instance, nullptr);

        // GLFW
		glfwDestroyWindow(m_foundation.window);
        glfwTerminate();
	}
}

}
