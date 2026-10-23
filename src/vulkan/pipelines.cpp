#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/logs.hpp"

#include <fstream>
#include <string>

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

/* ------------------------------------------------------------ */
/* ------------------------ Internals ------------------------- */
/* ------------------------------------------------------------ */
namespace {

constexpr u32 SPIRV_MAGIC = 0x07230203u;
constexpr size_t SPIRV_HEADER_WORDS = 5;
constexpr u32 SPIRV_OP_ENTRY_POINT = 15;
constexpr u32 SPIRV_OP_FUNCTION_END = 56;

/*
 * Walks the instruction stream (first word of each instruction: word count in
 * the high 16 bits, opcode in the low 16). A file cut in the middle of an
 * instruction fails the walk; a file cut between instructions fails because a
 * complete module ends with OpFunctionEnd.
 */
bool spirvLooksComplete(const std::vector<u32>& code, std::string& why) {
    size_t at = SPIRV_HEADER_WORDS;
    u32 lastOp = 0;
    bool hasEntryPoint = false;

    while (at < code.size()) {
        const u32 words = code[at] >> 16;
        const u32 op = code[at] & 0xFFFFu;
        if (words == 0 || at + words > code.size()) {
            why = "an instruction runs past the end of the file";
            return false;
        }

        if (op == SPIRV_OP_ENTRY_POINT) hasEntryPoint = true;
        lastOp = op;
        at += words;
    }

    if (!hasEntryPoint) {
        why = "no entry point";
        return false;
    }
    if (lastOp != SPIRV_OP_FUNCTION_END) {
        why = "the last instruction is not OpFunctionEnd";
        return false;
    }
    return true;
}

/*
 * Shared by the aborting and the non-aborting loader. `requireComplete` adds
 * the structural walk, which only the hot-reload path uses.
 */
bool readSpirv(
    const std::string& path,
    std::vector<u32>& code,
    std::string& error,
    bool requireComplete
) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        error = "Cannot open SPIR-V file '" + path + "'";
        return false;
    }

    const std::streamoff end = file.tellg();
    const size_t byteSize = end < 0 ? 0 : static_cast<size_t>(end);
    if (byteSize < SPIRV_HEADER_WORDS * sizeof(u32) || byteSize % sizeof(u32) != 0) {
        error = "'" + path + "' is not valid SPIR-V (bad size: " + std::to_string(byteSize) + " bytes)";
        return false;
    }

    code.resize(byteSize / sizeof(u32));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byteSize));
    if (!file) {
        error = "Cannot read SPIR-V file '" + path + "'";
        return false;
    }

    if (code[0] != SPIRV_MAGIC) {
        error = "'" + path + "' is not valid SPIR-V (missing magic number 0x07230203)";
        return false;
    }

    if (requireComplete) {
        std::string why;
        if (!spirvLooksComplete(code, why)) {
            error = "'" + path + "' is not a complete SPIR-V module (" + why + ")";
            return false;
        }
    }
    return true;
}

VkResult createModule(VkDevice device, const std::vector<u32>& code, VkShaderModule* out) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.pNext = nullptr;
    info.codeSize = code.size() * sizeof(u32);
    info.pCode = code.data();

    return vkCreateShaderModule(device, &info, nullptr, out);
}

VkResult createComputePipeline(
    VkDevice device,
    VkShaderModule module,
    VkPipelineLayout layout,
    VkPipeline* out
) {
    VkComputePipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.pNext = nullptr;
    info.layout = layout;
    info.stage = info::shaderStage(VK_SHADER_STAGE_COMPUTE_BIT, module);

    return vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, out);
}

// The fixed-function state lives here once, for both the build and the reload path
VkResult createGraphicsPipeline(
    VkDevice device,
    VkShaderModule vertModule,
    VkShaderModule fragModule,
    VkPipelineLayout layout,
    const interface::GraphicsState& state,
    VkFormat colorFormat,
    VkPipeline* out
) {
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

    return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, out);
}

} // namespace

/* ----------------------------------------------------------- */
/* ------------------------ Interface ------------------------ */
/* ----------------------------------------------------------- */
namespace interface {

BuiltPipeline PipelineBuilder::buildCompute(
    const std::string& computePath,
    VkDescriptorSetLayout setLayout,
    const VkPushConstantRange* push
) const {
    VkShaderModule computeModule = createShaderModule(m_device, computePath);
    VkPipelineLayout layout = createPipelineLayout(m_device, setLayout, push);

    VkPipeline pipeline;
    VK_CHECK(createComputePipeline(m_device, computeModule, layout, &pipeline));

    vkDestroyShaderModule(m_device, computeModule, nullptr);
    return BuiltPipeline{ pipeline, layout };
}

BuiltPipeline PipelineBuilder::buildGraphics(
    const std::string& vertexPath,
    const std::string& fragmentPath,
    VkDescriptorSetLayout setLayout,
    const GraphicsState& state,
    VkFormat colorFormat,
    const VkPushConstantRange* push
) const {
    VkShaderModule vertModule = createShaderModule(m_device, vertexPath);
    VkShaderModule fragModule = createShaderModule(m_device, fragmentPath);
    VkPipelineLayout layout = createPipelineLayout(m_device, setLayout, push);

    VkPipeline pipeline;
    VK_CHECK(createGraphicsPipeline(
        m_device, vertModule, fragModule, layout, state, colorFormat, &pipeline
    ));

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
    return BuiltPipeline{ pipeline, layout };
}

VkPipeline PipelineBuilder::tryBuildCompute(
    const std::string& computePath,
    VkPipelineLayout layout
) const {
    VkShaderModule module = tryCreateShaderModule(m_device, computePath);
    if (module == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = createComputePipeline(m_device, module, layout, &pipeline);
    vkDestroyShaderModule(m_device, module, nullptr);

    if (result != VK_SUCCESS) {
        M_ERROR("Compute pipeline creation failed for '{}': {}", computePath, string_VkResult(result));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline PipelineBuilder::tryBuildGraphics(
    const std::string& vertexPath,
    const std::string& fragmentPath,
    VkPipelineLayout layout,
    const GraphicsState& state,
    VkFormat colorFormat
) const {
    VkShaderModule vertModule = tryCreateShaderModule(m_device, vertexPath);
    if (vertModule == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkShaderModule fragModule = tryCreateShaderModule(m_device, fragmentPath);
    if (fragModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, vertModule, nullptr);
        return VK_NULL_HANDLE;
    }

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = createGraphicsPipeline(
        m_device, vertModule, fragModule, layout, state, colorFormat, &pipeline
    );
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        M_ERROR("Graphics pipeline creation failed for '{}' + '{}': {}",
                vertexPath, fragmentPath, string_VkResult(result));
        return VK_NULL_HANDLE;
    }
    return pipeline;
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
    std::vector<u32> code;
    std::string error;
    if (!readSpirv(path, code, error, false))
        M_FATAL("{}", error);

    VkShaderModule module;
    VK_CHECK(createModule(device, code, &module));
    return module;
}

VkShaderModule tryCreateShaderModule(VkDevice device, const std::string& path) {
    std::vector<u32> code;
    std::string error;
    if (!readSpirv(path, code, error, true)) {
        M_ERROR("{}", error);
        return VK_NULL_HANDLE;
    }

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = createModule(device, code, &module);
    if (result != VK_SUCCESS) {
        M_ERROR("vkCreateShaderModule failed for '{}': {}", path, string_VkResult(result));
        return VK_NULL_HANDLE;
    }
    return module;
}

VkPipelineLayout createPipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout setLayout,
    const VkPushConstantRange* push
) {
    const bool hasSet = setLayout != VK_NULL_HANDLE;

    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.setLayoutCount = hasSet ? 1 : 0;
    info.pSetLayouts = hasSet ? &setLayout : nullptr; // setLayout is a by-value param, alive for the call
    info.pushConstantRangeCount = push != nullptr ? 1 : 0;
    info.pPushConstantRanges = push;

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
