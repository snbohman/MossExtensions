#include "moss/extensions/vulkan/attachments/passes/pass_system.hpp"
#include "moss/extensions/vulkan/attachments/passes/components.hpp"
#include "moss/extensions/vulkan/attachments/images/gpu_resources.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace moss::extensions::vulkan {

namespace {

constexpr u32 MAX_DOUBLE_BUFFERED_PER_PASS = 4;

/*
 * Optional components have no direct support in moss: run a DynamicQuery over
 * a one-element DynamicView and check for empty. Returns a copy.
 */
template<typename T, typename K>
std::optional<T> findOn(const K& key, Entity entity) {
    DynamicView one{ entity };
    auto rows = cmd::DynamicQuery<With<T>, cmd::View<Include<T>>>::init(key).atlas(one);
    if (rows.empty()) return std::nullopt;
    return std::optional<T>(std::get<0>(rows[0]));
}

/*
 * Which physical side of an image a binding resolves to for a given mask.
 * Bit j of the mask is the current read side of D[j]. read and sample bind the
 * read side, write binds the other one. Single-buffered images have one side.
 */
u32 resolveSide(
    const std::vector<std::string>& doubleBuffered,
    const Binding& binding,
    const GpuResources::Image& image,
    u32 mask
) {
    if (!image.doubleBuffered) return 0;

    auto it = std::find(doubleBuffered.begin(), doubleBuffered.end(), binding.resource);
    const u32 j = u32(it - doubleBuffered.begin());
    const u32 readSide = (mask >> j) & 1u;
    return binding.access == BindAccess::Write ? 1u - readSide : readSide;
}

void writeDescriptorSet(
    VkDevice device,
    VkDescriptorSet set,
    const std::vector<Binding>& bindings,
    const std::vector<std::string>& doubleBuffered,
    u32 mask,
    const RenderCore& core,
    const GpuResources* gpu
) {
    // reserve() so the pointers handed to the writes below never get invalidated
    std::vector<VkDescriptorImageInfo> imageInfos;
    imageInfos.reserve(bindings.size());
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(bindings.size());

    for (const Binding& binding : bindings) {
        VkImageView view;
        if (binding.resource == DRAW_IMAGE_NAME) {
            view = core.drawImage.imageView;
        } else {
            const GpuResources::Image& image = gpu->images.at(binding.resource);
            view = image.sides[resolveSide(doubleBuffered, binding, image, mask)].imageView;
        }

        VkDescriptorImageInfo info = {};
        info.imageView = view;
        info.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // images stay in GENERAL during passes
        info.sampler = (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            ? gpu->sampler
            : VK_NULL_HANDLE;
        imageInfos.push_back(info);

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = set;
        write.dstBinding = binding.index;
        write.descriptorCount = 1;
        write.descriptorType = binding.type;
        write.pImageInfo = &imageInfos.back();
        writes.push_back(write);
    }

    vkUpdateDescriptorSets(device, (u32)writes.size(), writes.data(), 0, nullptr);
}

}

/* -------------------------------------------------------------------- */
/* --------------------------- ECS gathering --------------------------- */
/* -------------------------------------------------------------------- */

std::vector<PassSystem::PassConfig> PassSystem::gather(
    const Key<key::WRITE>& key,
    const DynamicView& entities
) {
    std::vector<PassConfig> passes;

    for (Entity entity : entities) {
        auto compute  = findOn<ComputeShader>(key, entity);
        auto vertex   = findOn<VertexShader>(key, entity);
        auto fragment = findOn<FragmentShader>(key, entity);

        // No shader component at all: not a pass
        if (!compute && !vertex && !fragment) continue;

        const u32 index = u32(passes.size());

        if (compute && (vertex || fragment))
            M_FATAL("pass #{} ({}): a ComputeShader cannot be combined with a VertexShader or FragmentShader",
                    index, compute->path);
        if (!compute && (vertex.has_value() != fragment.has_value()))
            M_FATAL("pass #{} ({}): a graphics pass needs both a VertexShader and a FragmentShader",
                    index, vertex ? vertex->path : fragment->path);

        auto dispatch = findOn<Dispatch>(key, entity);
        auto bind     = findOn<Bind>(key, entity);
        auto repeat   = findOn<Repeat>(key, entity);
        auto once     = findOn<Once>(key, entity);
        auto push     = findOn<PushConstants>(key, entity);
        auto state    = findOn<PipelineState>(key, entity);
        auto draw     = findOn<DrawCall>(key, entity);

        PassConfig config;
        config.index = index;
        config.entity = entity;

        if (compute) {
            config.kind = Kind::Compute;
            config.computePath = compute->path;
            config.label = "pass #" + std::to_string(index) + " (" + compute->path + ")";

            if (!dispatch)
                M_FATAL("{}: a compute pass needs a Dispatch component", config.label);
            M_WARN_IF(state.has_value(), "{}: PipelineState is for graphics passes and is ignored", config.label);
            M_WARN_IF(draw.has_value(), "{}: DrawCall is for graphics passes and is ignored", config.label);

            config.dispatchFromImage = dispatch->fromImage;
            config.groupX = dispatch->groupX;
            config.groupY = dispatch->groupY;
            config.groupZ = dispatch->groupZ;
            config.dispatchImage = dispatch->image;
            config.localX = dispatch->localX;
            config.localY = dispatch->localY;
        } else {
            config.kind = Kind::Graphics;
            config.vertexPath = vertex->path;
            config.fragmentPath = fragment->path;
            config.label = "pass #" + std::to_string(index) + " (" + vertex->path + " + " + fragment->path + ")";

            M_WARN_IF(dispatch.has_value(), "{}: Dispatch is for compute passes and is ignored", config.label);

            if (state) config.state = state->state;
            if (draw) {
                config.vertexCount = draw->vertexCount;
                config.instanceCount = draw->instanceCount;
            }
        }

        if (bind) config.bindings = bind->bindings;
        if (repeat) config.repeat = repeat->count;
        config.once = once.has_value();
        if (push) {
            config.hasPush = true;
            config.pushSize = push->size;
        }

        passes.push_back(std::move(config));
    }

    return passes;
}

/* -------------------------------------------------------------------- */
/* ------------------------ Validate + analyse ------------------------- */
/* -------------------------------------------------------------------- */

/*
 * Validates one pass (M_FATAL on any problem) and computes everything that
 * does not need Vulkan objects: the double-buffered set D, which images are
 * written, the physical ids touched per mask, dispatch group counts and the
 * barrier stage/access flags.
 */
PassSystem::CompiledPass PassSystem::analyse(
    const PassConfig& config,
    const RenderCore& core,
    const GpuResources* gpu,
    const std::unordered_map<std::string, u32>& baseIds,
    u32 maxPushSize
) {
    const std::string& label = config.label;
    const bool graphics = config.kind == Kind::Graphics;

    CompiledPass pass;
    pass.kind = config.kind;
    pass.entity = config.entity;
    pass.entityView = { config.entity };
    pass.label = label;
    pass.repeat = config.repeat;
    pass.once = config.once;
    pass.hasPush = config.hasPush;
    pass.pushSize = config.pushSize;
    pass.pushStages = graphics
        ? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        : VK_SHADER_STAGE_COMPUTE_BIT;
    pass.vertexCount = config.vertexCount;
    pass.instanceCount = config.instanceCount;

    if (config.repeat == 0)
        M_FATAL("{}: Repeat(0) is invalid, the count must be at least 1", label);
    if (config.hasPush && config.pushSize > maxPushSize)
        M_FATAL("{}: push constants are {} bytes but the device allows only {}",
                label, config.pushSize, maxPushSize);
    if (!config.bindings.empty() && gpu == nullptr)
        M_FATAL("{}: the pass has bindings but GpuResources does not exist. "
                "Connect ImageSystem before PassSystem", label);

    /* ----------------------------- Bindings ----------------------------- */
    std::unordered_set<u32> usedIndices;
    std::unordered_set<std::string> sampledSingle;   // single-buffered (and "draw") images sampled
    std::unordered_set<std::string> writtenSingle;   // ... and written

    for (const Binding& binding : config.bindings) {
        if (!usedIndices.insert(binding.index).second)
            M_FATAL("{}: duplicate binding index {}", label, binding.index);

        const bool writes = binding.access == BindAccess::Write
                         || binding.access == BindAccess::ReadWrite;

        if (binding.resource == DRAW_IMAGE_NAME) {
            if (graphics)
                M_FATAL("{}: a graphics pass cannot bind '{}', it is the render target",
                        label, DRAW_IMAGE_NAME);
            if (binding.access == BindAccess::Sample) sampledSingle.insert(binding.resource);
            if (writes) writtenSingle.insert(binding.resource);
            continue;
        }

        auto it = gpu->images.find(binding.resource);
        if (it == gpu->images.end())
            M_FATAL("{}: binding {} references image '{}' but no SimImage entity has that name",
                    label, binding.index, binding.resource);
        const GpuResources::Image& image = it->second;

        if (!image.doubleBuffered) {
            if (binding.access == BindAccess::Sample) sampledSingle.insert(binding.resource);
            if (writes) writtenSingle.insert(binding.resource);
            continue;
        }

        // Double-buffered image
        if (binding.access == BindAccess::ReadWrite)
            M_FATAL("{}: binding {} uses readWrite on the double-buffered image '{}'. "
                    "Use read and write (they hit different sides)",
                    label, binding.index, binding.resource);

        if (std::find(pass.doubleBuffered.begin(), pass.doubleBuffered.end(), binding.resource)
            == pass.doubleBuffered.end()) {
            pass.doubleBuffered.push_back(binding.resource);
            if (pass.doubleBuffered.size() > MAX_DOUBLE_BUFFERED_PER_PASS)
                M_FATAL("{}: binds more than {} distinct double-buffered images",
                        label, MAX_DOUBLE_BUFFERED_PER_PASS);
        }

        if (binding.access == BindAccess::Write) {
            if (std::find(pass.written.begin(), pass.written.end(), binding.resource) != pass.written.end())
                M_FATAL("{}: the double-buffered image '{}' is written by two bindings",
                        label, binding.resource);
            pass.written.push_back(binding.resource);
        }
    }

    for (const std::string& name : writtenSingle) {
        M_WARN_IF(sampledSingle.count(name) > 0,
            "{}: samples '{}' and also writes it. Reads may see torn data; "
            "make it DoubleBuffered or split the pass", label, name);
    }

    /* ----------------------------- Dispatch ----------------------------- */
    if (!graphics) {
        if (config.dispatchFromImage) {
            if (config.localX == 0 || config.localY == 0)
                M_FATAL("{}: Dispatch local size must not be 0", label);

            VkExtent3D extent;
            if (config.dispatchImage == DRAW_IMAGE_NAME) {
                extent = core.drawImage.imageExtent;
            } else {
                if (gpu == nullptr)
                    M_FATAL("{}: Dispatch references image '{}' but GpuResources does not exist. "
                            "Connect ImageSystem before PassSystem", label, config.dispatchImage);
                auto it = gpu->images.find(config.dispatchImage);
                if (it == gpu->images.end())
                    M_FATAL("{}: Dispatch references image '{}' but no SimImage entity has that name",
                            label, config.dispatchImage);
                extent = it->second.sides[0].imageExtent;
            }

            pass.groupX = (extent.width + config.localX - 1) / config.localX;
            pass.groupY = (extent.height + config.localY - 1) / config.localY;
            pass.groupZ = 1;
        } else {
            pass.groupX = config.groupX;
            pass.groupY = config.groupY;
            pass.groupZ = config.groupZ;
        }
    }

    /* ------------------------- Barrier bookkeeping ------------------------ */
    const u32 maskCount = 1u << pass.doubleBuffered.size();
    pass.touched.resize(maskCount);
    for (u32 mask = 0; mask < maskCount; mask++) {
        std::vector<u32>& ids = pass.touched[mask];

        for (const Binding& binding : config.bindings) {
            if (binding.resource == DRAW_IMAGE_NAME) {
                ids.push_back(0);
                continue;
            }
            const GpuResources::Image& image = gpu->images.at(binding.resource);
            ids.push_back(baseIds.at(binding.resource) + resolveSide(pass.doubleBuffered, binding, image, mask));
        }

        // A graphics pass renders into the draw image whether it binds it or not
        if (graphics) ids.push_back(0);

        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }

    // Stages/accesses used when this pass is the source/destination of a barrier
    if (!graphics) {
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

    return pass;
}

/* -------------------------------------------------------------------- */
/* ------------------------------- Build ------------------------------- */
/* -------------------------------------------------------------------- */

void PassSystem::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    GpuResources* gpu = cmd::Resource<GpuResources>::init(key).find();   // null only if no pass has bindings
    VkDevice device = core.foundation.device;

    // Copy everything we need out of the ECS before touching Vulkan
    std::vector<PassConfig> configs = gather(key, entities);

    // Physical resource ids for barrier tracking: 0 is the draw image, then
    // one id per image side (name#0, name#1)
    std::unordered_map<std::string, u32> baseIds;
    if (gpu != nullptr) {
        u32 next = 1;
        for (const auto& entry : gpu->images) {
            baseIds[entry.first] = next;
            next += entry.second.doubleBuffered ? 2 : 1;
        }
    }

    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(core.foundation.physicalDevice, &deviceProps);

    // Validate everything before creating a single Vulkan object
    m_passes.clear();
    m_passes.reserve(configs.size());
    for (const PassConfig& config : configs)
        m_passes.push_back(analyse(config, core, gpu, baseIds, deviceProps.limits.maxPushConstantsSize));

    /* --------------------------- Descriptor pool -------------------------- */
    // Total sets, and per descriptor type the largest count any single set
    // needs. The "ratios" are integral on purpose (DescriptorAllocator::init
    // computes uint32_t(ratio * maxSets), which would truncate a fraction).
    // This over-allocates slightly, deliberately.
    u32 totalSets = 0;
    std::map<VkDescriptorType, u32> maxPerSet;
    for (size_t i = 0; i < configs.size(); i++) {
        if (configs[i].bindings.empty()) continue;

        totalSets += 1u << m_passes[i].doubleBuffered.size();

        std::map<VkDescriptorType, u32> perSet;
        for (const Binding& binding : configs[i].bindings) perSet[binding.type] += 1;
        for (const auto& [type, count] : perSet)
            maxPerSet[type] = std::max(maxPerSet[type], count);
    }

    // Local on purpose: the pool handle is all that is needed afterwards
    utils::interface::DescriptorAllocator allocator = {};
    if (totalSets > 0) {
        std::vector<utils::interface::DescriptorAllocator::PoolSizeRatio> sizes;
        for (const auto& [type, count] : maxPerSet)
            sizes.push_back({ type, float(count) });

        allocator.init(device, totalSets, sizes);
        core.dqueue.push([device, pool = allocator.pool]() {
            vkDestroyDescriptorPool(device, pool, nullptr);
        });
    }

    /* ------------------- Layouts, sets and pipelines --------------------- */
    utils::interface::PipelineBuilder builder(device);

    for (size_t i = 0; i < configs.size(); i++) {
        const PassConfig& config = configs[i];
        CompiledPass& pass = m_passes[i];
        const bool graphics = config.kind == Kind::Graphics;

        // A pass without Bind has no set layout and no set
        if (!config.bindings.empty()) {
            std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
            layoutBindings.reserve(config.bindings.size());
            for (const Binding& binding : config.bindings) {
                layoutBindings.push_back(VkDescriptorSetLayoutBinding{
                    .binding = binding.index,
                    .descriptorType = binding.type,
                    .descriptorCount = 1,
                    // Stage flags are OR'd in by createDescriptorSetLayout
                    .stageFlags = 0,
                    .pImmutableSamplers = nullptr
                });
            }

            VkShaderStageFlags stages = graphics
                ? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                : VK_SHADER_STAGE_COMPUTE_BIT;

            pass.setLayout = utils::createDescriptorSetLayout(device, std::move(layoutBindings), stages);
            core.dqueue.push([device, layout = pass.setLayout]() {
                vkDestroyDescriptorSetLayout(device, layout, nullptr);
            });

            // One set per mask: bit j is the current read side of D[j]
            const u32 maskCount = 1u << pass.doubleBuffered.size();
            pass.sets.reserve(maskCount);
            for (u32 mask = 0; mask < maskCount; mask++) {
                VkDescriptorSet set = allocator.allocate(device, pass.setLayout);
                writeDescriptorSet(device, set, config.bindings, pass.doubleBuffered, mask, core, gpu);
                pass.sets.push_back(set);
            }
        }

        // Push-constant range: stages come from the pass kind, offset 0
        VkPushConstantRange pushRange = { pass.pushStages, 0, pass.pushSize };
        const VkPushConstantRange* push = pass.hasPush ? &pushRange : nullptr;

        utils::interface::BuiltPipeline built;
        if (!graphics) {
            built = builder.buildCompute(config.computePath, pass.setLayout, push);
        } else {
            // Graphics passes always render into the draw image
            built = builder.buildGraphics(
                config.vertexPath, config.fragmentPath, pass.setLayout,
                config.state, core.drawImage.imageFormat, push
            );
        }

        pass.pipeline = built.pipeline;
        pass.layout = built.layout;

        core.dqueue.push([device, built]() {
            vkDestroyPipeline(device, built.pipeline, nullptr);
            vkDestroyPipelineLayout(device, built.layout, nullptr);
        });
    }
}

/* -------------------------------------------------------------------- */
/* ------------------------------- Record ------------------------------ */
/* -------------------------------------------------------------------- */

/*
 * Records every pass in order, repeating each `repeat` times.
 *
 * Everything here reads/writes the draw image or sim images while they are in
 * GENERAL (FrameBegin puts the draw image there, ImageSystem puts the sim
 * images there once). Graphics passes render INTO the draw image with dynamic
 * rendering, also in GENERAL.
 *
 * Barriers (linear, no frame graph): remember which physical images have been
 * touched since the last barrier. When a pass touches one of them again, emit
 * one memory barrier covering everything pending, then carry on.
 */
void PassSystem::tick(const Key<key::READ>& key) {
    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    GpuResources* gpu = cmd::Resource<GpuResources>::init(key).find();   // null only if no pass has bindings
    VkCommandBuffer cmdBuf = core.frame.cmd;

    // Attachment contract: first call. Also covers the previous frame's passes
    // (sim state is read again this frame) and ImageSystem's first-frame clear.
    utils::syncAll(cmdBuf);

    std::unordered_set<u32> pending;
    VkPipelineStageFlags2 pendingStages = 0;
    VkAccessFlags2 pendingWrites = 0;

    for (CompiledPass& pass : m_passes) {
        if (pass.once && pass.onceDone) continue;

        const bool graphics = pass.kind == Kind::Graphics;
        const VkPipelineBindPoint bindPoint = graphics
            ? VK_PIPELINE_BIND_POINT_GRAPHICS
            : VK_PIPELINE_BIND_POINT_COMPUTE;

        // Fetched every tick: a user system may have edited it since last frame
        PushConstants* pushConstants = nullptr;
        if (pass.hasPush) {
            auto rows = cmd::DynamicQuery<With<
                PushConstants
            >, cmd::View<
                Include<PushConstants>
            >>::init(key).atlas(pass.entityView);
            if (rows.empty())
                M_FATAL("{}: the PushConstants component is gone", pass.label);

            pushConstants = &std::get<0>(rows[0]);
            if (pushConstants->size != pass.pushSize)
                M_FATAL("{}: PushConstants changed size after build ({} -> {})",
                        pass.label, pass.pushSize, pushConstants->size);
        }

        for (u32 i = 0; i < pass.repeat; i++) {
            // Bit j = current read side of D[j]
            u32 mask = 0;
            for (size_t j = 0; j < pass.doubleBuffered.size(); j++)
                mask |= gpu->images.at(pass.doubleBuffered[j]).readSide << j;

            const std::vector<u32>& touched = pass.touched[mask];

            bool hazard = false;
            for (u32 id : touched) {
                if (pending.count(id)) { hazard = true; break; }
            }

            if (hazard) {
                utils::memoryBarrier(
                    cmdBuf,
                    pendingStages, pendingWrites,
                    pass.stage, pass.readWriteAccess
                );
                pending.clear();
                pendingStages = 0;
                pendingWrites = 0;
            }

            pending.insert(touched.begin(), touched.end());
            pendingStages |= pass.stage;
            pendingWrites |= pass.writeAccess;

            if (!graphics) {
                vkCmdBindPipeline(cmdBuf, bindPoint, pass.pipeline);
                if (pass.setLayout != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(
                        cmdBuf, bindPoint, pass.layout, 0, 1, &pass.sets[mask], 0, nullptr
                    );
                }
                if (pushConstants != nullptr) {
                    vkCmdPushConstants(
                        cmdBuf, pass.layout, pass.pushStages, 0, pushConstants->size, pushConstants->data
                    );
                }
                vkCmdDispatch(cmdBuf, pass.groupX, pass.groupY, pass.groupZ);
            } else {
                utils::beginRendering(
                    cmdBuf, core.drawImage.imageView, VK_IMAGE_LAYOUT_GENERAL, core.drawImage.drawExtent
                );
                utils::setViewportScissor(cmdBuf, core.drawImage.drawExtent);
                vkCmdBindPipeline(cmdBuf, bindPoint, pass.pipeline);
                if (pass.setLayout != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(
                        cmdBuf, bindPoint, pass.layout, 0, 1, &pass.sets[mask], 0, nullptr
                    );
                }
                if (pushConstants != nullptr) {
                    vkCmdPushConstants(
                        cmdBuf, pass.layout, pass.pushStages, 0, pushConstants->size, pushConstants->data
                    );
                }
                vkCmdDraw(cmdBuf, pass.vertexCount, pass.instanceCount, 0, 0);
                vkCmdEndRendering(cmdBuf);
            }

            // The write side becomes the read side for whatever comes next
            for (const std::string& name : pass.written)
                gpu->images.at(name).readSide ^= 1u;
        }

        if (pass.once) pass.onceDone = true;
    }
}

}
