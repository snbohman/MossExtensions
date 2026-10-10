#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/logs.hpp"

#include <fstream>
#include <stdexcept>

namespace moss::extensions::vulkan::utils {

/* ------------------------------------------------------ */
/* ------------------------ Info ------------------------ */
/* ------------------------------------------------------ */
namespace info {

VkPipelineShaderStageCreateInfo shaderStage(
    VkShaderStageFlagBits stage,
    VkShaderModule module
) {
    VkPipelineShaderStageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.pNext = nullptr;
    info.stage = stage;
    info.module = module;
    info.pName = "main";

    return info;
}

} // namespace info

/* ----------------------------------------------------------- */
/* ------------------------ Interface ------------------------ */
/* ----------------------------------------------------------- */
namespace interface {

BuiltPipeline PipelineBuilder::buildCompute(
    const std::string& computePath,
    VkDescriptorSetLayout setLayout
) const {
    VkShaderModule computeModule = createShaderModule(m_device, computePath);
    VkPipelineLayout layout = createPipelineLayout(m_device, setLayout);

    VkComputePipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.pNext = nullptr;
    info.layout = layout;
    info.stage = info::shaderStage(VK_SHADER_STAGE_COMPUTE_BIT, computeModule);

    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(
        m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline
    ));

    vkDestroyShaderModule(m_device, computeModule, nullptr);
    return BuiltPipeline{ pipeline, layout };
}

BuiltPipeline PipelineBuilder::buildGraphics(
    const std::string& vertexPath,
    const std::string& fragmentPath,
    VkDescriptorSetLayout setLayout,
    const PipelineState& state,
    VkFormat colorFormat
) const {
    VkShaderModule vertModule = createShaderModule(m_device, vertexPath);
    VkShaderModule fragModule = createShaderModule(m_device, fragmentPath);
    VkPipelineLayout layout = createPipelineLayout(m_device, setLayout);

    VkPipelineShaderStageCreateInfo stages[2] = {
        info::shaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertModule),
        info::shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragModule)
    };

    // No vertex buffers: the vertex shader derives positions from gl_VertexIndex
    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = state.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Actual viewport/scissor are dynamic; only the counts are baked in
    VkPipelineViewportStateCreateInfo viewport = {};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = state.cullMode;
    raster.frontFace = state.frontFace;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth/stencil attachment, so this is all off
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = state.blendEnable ? VK_TRUE : VK_FALSE;
    if (state.blendEnable) {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    // Dynamic rendering: attachment formats go in pNext, there is no VkRenderPass
    VkPipelineRenderingCreateInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    info.renderPass = VK_NULL_HANDLE;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(
        m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline
    ));

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
    return BuiltPipeline{ pipeline, layout };
}

} // namespace interface

/* ------------------------------------------------------------ */
/* ------------------------ Non return ------------------------ */
/* ------------------------------------------------------------ */

VkDescriptorSetLayout createDescriptorSetLayout(
    VkDevice device,
    std::vector<VkDescriptorSetLayoutBinding> bindings,
    VkShaderStageFlags shaderStages
) {
    for (auto& b : bindings)
        b.stageFlags |= shaderStages;

    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.bindingCount = (u32)bindings.size();
    info.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
    return layout;
}

VkShaderModule createShaderModule(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open SPIR-V file '" + path + "'");

    const size_t byteSize = static_cast<size_t>(file.tellg());
    if (byteSize == 0 || byteSize % sizeof(u32) != 0)
        throw std::runtime_error("'" + path + "' is not valid SPIR-V (bad size)");

    std::vector<u32> code(byteSize / sizeof(u32));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byteSize));

    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.pNext = nullptr;
    info.codeSize = byteSize;
    info.pCode = code.data();

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout) {
    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.setLayoutCount = 1;
    info.pSetLayouts = &setLayout; // setLayout is a by-value param, alive for the call

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &layout));
    return layout;
}

void memoryBarrier(
    VkCommandBuffer cmd,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess
) {
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.pNext = nullptr;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;

    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void beginRendering(
    VkCommandBuffer cmd,
    VkImageView colorView,
    VkImageLayout colorLayout,
    VkExtent2D extent
) {
    VkRenderingAttachmentInfo color = {};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.pNext = nullptr;
    color.imageView = colorView;
    color.imageLayout = colorLayout;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.pNext = nullptr;
    rendering.renderArea = VkRect2D{ VkOffset2D{ 0, 0 }, extent };
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;

    vkCmdBeginRendering(cmd, &rendering);
}

void setViewportScissor(VkCommandBuffer cmd, VkExtent2D extent) {
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = { VkOffset2D{ 0, 0 }, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

} // namespace moss::extensions::vulkan::utils
