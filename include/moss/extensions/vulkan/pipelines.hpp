#pragma once

#include "moss/extensions/vulkan/meta.hpp"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <string>
#include <vector>

namespace moss::extensions::vulkan::utils {

/* ------------------------------------------------------ */
/* ------------------------ Info ------------------------ */
/* ------------------------------------------------------ */
namespace info {

// pName points at a string literal, so the result has no lifetime hazard.
VkPipelineShaderStageCreateInfo shaderStage(
    VkShaderStageFlagBits stage,
    VkShaderModule module
);

} // namespace info

/* ----------------------------------------------------------- */
/* ------------------------ Interface ------------------------ */
/* ----------------------------------------------------------- */
namespace interface {

struct BuiltPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

/*
 * Minimal fixed-function state for the graphics pipeline. Plain data with no
 * moss dependency, so the generic builder does not know about pass components
 * (the PipelineState component wraps this). Defaults describe the fullscreen
 * triangle: no vertex input (gl_VertexIndex), no depth test, no culling, no
 * blending. The colour format is passed separately (the draw image's).
 */
struct GraphicsState {
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags     cullMode = VK_CULL_MODE_NONE;
    VkFrontFace         frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool                blendEnable = false;
};

/*
 * Loads SPIR-V -> VkShaderModule -> VkPipelineLayout (from an existing
 * descriptor set layout) -> vkCreateComputePipelines / vkCreateGraphicsPipelines.
 * Graphics pipelines use dynamic rendering (VkPipelineRenderingCreateInfo),
 * dynamic viewport/scissor, and no vertex input. Shader modules are destroyed
 * before returning. The caller owns the returned pipeline + layout.
 *
 * setLayout == VK_NULL_HANDLE means "no bindings": the pipeline layout then
 * has zero set layouts. When `push` is non-null it adds one push-constant
 * range to the layout.
 *
 * build*: log, then abort on any failure (the extension's error policy).
 *
 * tryBuild*: the ONE exception to that policy, for shader hot reload. They
 * build only the pipeline, against a layout that already exists (the layout
 * never changes on a reload), and on any failure log with M_ERROR and return
 * VK_NULL_HANDLE. The caller owns a non-null result. They also reject SPIR-V
 * that is visibly incomplete (see tryCreateShaderModule).
 */
class PipelineBuilder {
public:
    explicit PipelineBuilder(VkDevice device) : m_device(device) { }

    BuiltPipeline buildCompute(
        const std::string& computePath,
        VkDescriptorSetLayout setLayout,
        const VkPushConstantRange* push = nullptr
    ) const;

    BuiltPipeline buildGraphics(
        const std::string& vertexPath,
        const std::string& fragmentPath,
        VkDescriptorSetLayout setLayout,
        const GraphicsState& state,
        VkFormat colorFormat,
        const VkPushConstantRange* push = nullptr
    ) const;

    VkPipeline tryBuildCompute(
        const std::string& computePath,
        VkPipelineLayout layout
    ) const;

    VkPipeline tryBuildGraphics(
        const std::string& vertexPath,
        const std::string& fragmentPath,
        VkPipelineLayout layout,
        const GraphicsState& state,
        VkFormat colorFormat
    ) const;

private:
    VkDevice m_device;
};

} // namespace interface

/* ------------------------------------------------------------ */
/* ------------------------ Non return ------------------------ */
/* ------------------------------------------------------------ */

// Owns its input (by value) and creates the final handle, so no dangling pBindings.
VkDescriptorSetLayout createDescriptorSetLayout(
    VkDevice device,
    std::vector<VkDescriptorSetLayoutBinding> bindings,
    VkShaderStageFlags shaderStages
);

// Logs the path and aborts when the file is missing or not valid SPIR-V
// (size, magic number).
VkShaderModule createShaderModule(VkDevice device, const std::string& path);

/*
 * Non-aborting twin of createShaderModule, for hot reload only. Logs and
 * returns VK_NULL_HANDLE when the file cannot be read, is too small, has a
 * size that is not a multiple of 4, lacks the SPIR-V magic number, or is
 * visibly incomplete: the instruction stream must end exactly at the end of
 * the file, contain an OpEntryPoint and end with OpFunctionEnd. This is a
 * cheap truncation check, not a validator.
 */
VkShaderModule tryCreateShaderModule(VkDevice device, const std::string& path);

VkPipelineLayout createPipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout setLayout,
    const VkPushConstantRange* push = nullptr
);

// Global execution + memory dependency (synchronization2).
void memoryBarrier(
    VkCommandBuffer cmd,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess
);

// Single colour attachment, LOAD/STORE, no depth. Pair with vkCmdEndRendering.
void beginRendering(
    VkCommandBuffer cmd,
    VkImageView colorView,
    VkImageLayout colorLayout,
    VkExtent2D extent
);

void setViewportScissor(VkCommandBuffer cmd, VkExtent2D extent);

} // namespace moss::extensions::vulkan::utils
