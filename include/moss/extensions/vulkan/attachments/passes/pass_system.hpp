#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/attachments/images/gpu_resources.hpp"
#include "moss/extensions/vulkan/attachments/passes/components.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace moss::extensions::vulkan {

/*
 * The first attachment: gathers every pass entity of its Context (creation
 * order = pass order), validates and compiles them (descriptors, pipelines),
 * and records them every frame.
 *
 * Connect it after ImageSystem and before FrameEnd. A user system that writes
 * component data PassSystem reads (PushConstants, for example) connects
 * BEFORE PassSystem:
 *
 *     mirror.connect<
 *         vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem, vk::FrameEnd
 *     >();
 *
 * A pass with bindings needs GpuResources, so ImageSystem must be connected
 * before PassSystem in that case. The descriptor pool is private to this
 * system. Everything it creates is destroyed through core.dqueue; it has no
 * exit of its own.
 *
 * Barriers are linear, not a frame graph: the system remembers which physical
 * images were touched since the last barrier, and when a pass touches one of
 * them again (including a repeat of itself) it emits one memory barrier
 * covering everything pending.
 */
class PassSystem : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;

private:
    enum class Kind { Compute, Graphics };

    /* Plain copies of ECS data, so the queries are separate from Vulkan init */
    struct PassConfig {
        u32 index = 0;                       // among passes, in creation order
        Entity entity = entt::null;
        Kind kind = Kind::Compute;
        std::string label;                   // "pass #2 (shaders/x.comp.spv)"

        std::string computePath;
        std::string vertexPath;
        std::string fragmentPath;

        std::vector<Binding> bindings;

        bool dispatchFromImage = false;
        u32 groupX = 1, groupY = 1, groupZ = 1;
        std::string dispatchImage;
        u32 localX = 16, localY = 16;

        utils::interface::GraphicsState state;
        u32 vertexCount = 3, instanceCount = 1;

        u32 repeat = 1;
        bool once = false;
        bool hasPush = false;
        u32 pushSize = 0;
    };

    /* Everything the record loop needs for one pass */
    struct CompiledPass {
        Kind kind = Kind::Compute;
        Entity entity = entt::null;
        DynamicView entityView;              // one element, for the per-tick PushConstants lookup
        std::string label;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;   // null: the pass has no bindings
        std::vector<VkDescriptorSet> sets;                  // 2^k sets, indexed by mask

        bool hasPush = false;
        u32 pushSize = 0;
        VkShaderStageFlags pushStages = 0;

        u32 repeat = 1;
        bool once = false;
        bool onceDone = false;

        u32 groupX = 1, groupY = 1, groupZ = 1;
        u32 vertexCount = 3, instanceCount = 1;

        // D: distinct double-buffered images the pass binds. Bit j of a mask is
        // the current read side of D[j]; the pass has 2^|D| descriptor sets.
        std::vector<std::string> doubleBuffered;
        std::vector<std::string> written;                   // swapped after each iteration

        // Physical resource ids touched, per mask (id 0 is the draw image)
        std::vector<std::vector<u32>> touched;

        VkPipelineStageFlags2 stage = 0;
        VkAccessFlags2 writeAccess = 0;
        VkAccessFlags2 readWriteAccess = 0;
    };

    static std::vector<PassConfig> gather(const Key<key::WRITE>& key, const DynamicView& entities);

    static CompiledPass analyse(
        const PassConfig& config,
        const RenderCore& core,
        const GpuResources* gpu,
        const std::unordered_map<std::string, u32>& baseIds,
        u32 maxPushSize
    );

    std::vector<CompiledPass> m_passes = {};   // creation order
};

}
