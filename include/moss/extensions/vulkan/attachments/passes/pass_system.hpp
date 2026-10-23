#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/attachments/images/gpu_resources.hpp"
#include "moss/extensions/vulkan/attachments/passes/components.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
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
 * images and buffers were touched since the last barrier, and when a pass
 * touches one of them again (including a repeat of itself) it emits one memory
 * barrier covering everything pending.
 *
 * Resize: the draw image follows the window. At the start of tick the system
 * compares core.swapchainGeneration with its own copy; when it differs it
 * recomputes the group counts of Dispatch("draw") passes and rewrites every
 * descriptor set that binds "draw".
 *
 * Hot reload (opt-in, see HotReload in components.hpp): when one entity of the
 * Context carries HotReload, the system polls the passes' SPIR-V files at the
 * start of tick, before recording, and rebuilds the pipelines of the passes
 * whose files changed. It replaces the pipeline only: the layout, sets, push
 * range and bindings stay, so a shader whose interface changed needs a
 * restart. A failed reload logs and keeps the old pipeline.
 */
class PassSystem : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;

private:
    enum class Kind { Compute, Graphics };

    /*
     * The pipeline lives behind a pointer so a hot reload can swap it. The
     * build-time deletor captures this pointer (never `this`) and destroys
     * whatever handle is current when core.dqueue is flushed, so a reload
     * can never cause a double destroy at exit.
     */
    struct PipelineHolder {
        VkPipeline handle = VK_NULL_HANDLE;
    };

    /* One SPIR-V file, shared by however many passes use it, stat'd once per poll */
    struct WatchedFile {
        std::string path;
        std::filesystem::file_time_type observed = {};   // mtime at the last poll
        std::filesystem::file_time_type accepted = {};   // mtime of the last settled version
        u32 version = 0;                                 // bumped when a new version settles
    };

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

        std::shared_ptr<PipelineHolder> pipeline;           // swapped by a hot reload
        VkPipelineLayout layout = VK_NULL_HANDLE;           // never changes
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

        // Kept for the resize refresh
        std::vector<Binding> bindings;
        bool usesDraw = false;               // some image binding is "draw"
        bool dispatchOnDraw = false;         // Dispatch("draw", ...): groups follow the window
        u32 localX = 16, localY = 16;

        // Kept for hot reload: what a rebuild of the pipeline needs
        std::string computePath;
        std::string vertexPath;
        std::string fragmentPath;
        utils::interface::GraphicsState state;
        std::vector<u32> watched;            // indices into m_watched (empty: hot reload off)
        std::vector<u32> builtVersions;      // version of each watched file the pipeline came from

        // D: distinct double-buffered resources (images or buffers) the pass
        // binds. Bit j of a mask is the current read side of D[j]; the pass has
        // 2^|D| descriptor sets.
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
        const VkPhysicalDeviceLimits& limits
    );

    // Re-derives everything that depends on the draw image after a resize
    void refresh(const RenderCore& core, const GpuResources* gpu);

    // Hot reload: poll the files, rebuild the pipelines of the passes that changed
    void pollShaders(const RenderCore& core);

    std::vector<CompiledPass> m_passes = {};   // creation order
    u32 m_generation = 0;                      // RenderCore::swapchainGeneration last seen

    bool m_hotReload = false;                  // a HotReload component was found
    std::chrono::milliseconds m_pollInterval{ 500 };
    std::chrono::steady_clock::time_point m_lastPoll = {};
    std::vector<WatchedFile> m_watched = {};   // one entry per distinct SPIR-V path
};

}
