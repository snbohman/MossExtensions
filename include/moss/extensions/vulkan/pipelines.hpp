#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/components.hpp"

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
 * Loads SPIR-V -> VkShaderModule -> VkPipelineLayout (from an existing
 * descriptor set layout) -> vkCreateComputePipelines / vkCreateGraphicsPipelines.
 * Graphics pipelines use dynamic rendering (VkPipelineRenderingCreateInfo),
 * dynamic viewport/scissor, and no vertex input. Shader modules are destroyed
 * before returning. The caller owns the returned pipeline + layout.
 */
class PipelineBuilder {
public:
    explicit PipelineBuilder(VkDevice device) : m_device(device) { }

    BuiltPipeline buildCompute(
        const std::string& computePath,
        VkDescriptorSetLayout setLayout
    ) const;

    BuiltPipeline buildGraphics(
        const std::string& vertexPath,
        const std::string& fragmentPath,
        VkDescriptorSetLayout setLayout,
        const PipelineState& state,
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

VkShaderModule createShaderModule(VkDevice device, const std::string& path);
VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout);

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
