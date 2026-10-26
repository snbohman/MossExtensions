#include "moss/extensions/imgui/systems.hpp"
#include "imgui/imgui.h"
#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"

#include <algorithm>
#include <set>

namespace moss::extensions::imgui {

namespace {

/*
 * The backend keeps a pointer to the colour format for as long as it lives (it
 * rebuilds pipelines from it), so it cannot be a local of build(). There is
 * one ImGui context per process anyway.
 */
VkFormat g_colorFormat = VK_FORMAT_UNDEFINED;

void checkResult(VkResult result) {
    VK_CHECK(result);
}

/*
 * The InitInfo layout changed between imgui versions: the dynamic rendering
 * fields moved into PipelineInfoMain, and ApiVersion was added. Each field is
 * set only if this imgui has it, so the same code builds on both layouts.
 */
template<typename Info>
void configureInfo(Info& info, const VkPipelineRenderingCreateInfo& rendering) {
    if constexpr (requires { info.ApiVersion; })
        info.ApiVersion = VK_API_VERSION_1_3;

    if constexpr (requires { info.UseDynamicRendering; })
        info.UseDynamicRendering = true;

    if constexpr (requires { info.PipelineInfoMain; }) {
        if constexpr (requires { info.PipelineInfoMain.UseDynamicRendering; })
            info.PipelineInfoMain.UseDynamicRendering = true;
        info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    } else {
        info.PipelineRenderingCreateInfo = rendering;
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    }
}

}

void Begin::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    vulkan::RenderCore& core = cmd::Resource<vulkan::RenderCore>::init(key).get();
    VkDevice device = core.foundation.device;

    M_FATAL_IF(ImGui::GetCurrentContext() != nullptr,
        "ig::Begin: an ImGui context already exists (is ig::Begin connected twice?)");

    Settings settings;
    auto rows = cmd::Query<With<Settings>>::init(key).atlas(entities);
    M_WARN_IF(rows.size() > 1,
        "{} entities carry ig::Settings; ig::Begin uses the first and ignores the rest",
        rows.size()
    );
    if (!rows.empty()) settings = std::get<0>(rows[0]);

    /* ---------------------------- Context ---------------------------- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = settings.saveLayout ? "imgui.ini" : nullptr;
    if (settings.keyboardNav) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (settings.darkTheme) ImGui::StyleColorsDark();
    if (settings.docking || settings.dockspace) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    else ImGui::StyleColorsLight();
    m_dockspace = settings.dockspace;

    /* ---------------------------- Backends --------------------------- */
    // Callbacks are installed and chained onto whatever the window already has
    M_FATAL_IFF(!ImGui_ImplGlfw_InitForVulkan(core.foundation.window, true),
        "ImGui_ImplGlfw_InitForVulkan failed");

    // The backend frees descriptor sets of its own textures, hence FREE_DESCRIPTOR_SET
    constexpr u32 POOL_SIZE = 128;
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          POOL_SIZE },
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = POOL_SIZE;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
    core.dqueue.push([device, pool]() {
        vkDestroyDescriptorPool(device, pool, nullptr);
    });

    g_colorFormat = core.drawImage.imageFormat;

    VkPipelineRenderingCreateInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &g_colorFormat;

    ImGui_ImplVulkan_InitInfo info = {};
    info.Instance = core.foundation.instance;
    info.PhysicalDevice = core.foundation.physicalDevice;
    info.Device = device;
    info.QueueFamily = core.foundation.queueFamily;
    info.Queue = core.foundation.graphicsQueue;
    info.DescriptorPool = pool;
    // The backend's vertex/index buffers are a ring of ImageCount: at least the
    // frames in flight (2), more is harmless
    info.MinImageCount = 2;
    info.ImageCount = std::max<u32>(2u, u32(core.swapchain.images.size()));
    info.CheckVkResultFn = checkResult;
    configureInfo(info, rendering);

    M_FATAL_IFF(!ImGui_ImplVulkan_Init(&info), "ImGui_ImplVulkan_Init failed");

    // Pushed AFTER the pool deletor: the queue runs LIFO, so the backends are
    // shut down while the pool, the device and the window still exist
    core.dqueue.push([]() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    });
}

void Begin::tick(const Key<key::READ>&) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (m_dockspace) {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    }
}

void End::tick(const Key<key::READ>& key) {
    M_FATAL_IF(ImGui::GetCurrentContext() == nullptr,
        "ig::End: there is no ImGui context. Connect ig::Begin before the UI systems");

    vulkan::RenderCore& core = cmd::Resource<vulkan::RenderCore>::init(key).get();
    VkCommandBuffer cmdBuf = core.frame.cmd;

    ImGui::Render();

    // Attachment contract: first call. Orders the passes' writes before the UI blends over them
    vulkan::utils::syncAll(cmdBuf);

    vulkan::utils::beginRendering(
        cmdBuf, core.drawImage.imageView, VK_IMAGE_LAYOUT_GENERAL, core.drawImage.drawExtent
    );
    // Always called, even for an empty frame: newer backends upload textures here
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
    vkCmdEndRendering(cmdBuf);
}

}
