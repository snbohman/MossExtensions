#include "moss/extensions/vulkan/attachments/passes/pass_system.hpp"
#include "moss/extensions/vulkan/attachments/passes/components.hpp"
#include "moss/extensions/vulkan/attachments/images/gpu_resources.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <system_error>
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

u32 groupCount(u32 extent, u32 local) {
    return (extent + local - 1) / local;
}

// Never call this for the draw image: it is in neither map
bool isDoubleBuffered(const GpuResources& gpu, const Binding& binding) {
    if (binding.target == BindTarget::Buffer) return gpu.buffers.at(binding.resource).doubleBuffered;
    return gpu.images.at(binding.resource).doubleBuffered;
}

// Names are unique across images and buffers, so at most one map has it
u32& readSideOf(GpuResources& gpu, const std::string& name) {
    auto image = gpu.images.find(name);
    if (image != gpu.images.end()) return image->second.readSide;
    return gpu.buffers.at(name).readSide;
}

/*
 * Which physical side of a resource a binding resolves to for a given mask.
 * Bit j of the mask is the current read side of D[j]. Read-style verbs bind the
 * read side, write binds the other one. Single-buffered resources have one side.
 */
u32 resolveSide(
    const std::vector<std::string>& doubleBuffered,
    const Binding& binding,
    bool isDouble,
    u32 mask
) {
    if (!isDouble) return 0;

    auto it = std::find(doubleBuffered.begin(), doubleBuffered.end(), binding.resource);
    const u32 j = u32(it - doubleBuffered.begin());
    const u32 readSide = (mask >> j) & 1u;
    return binding.access == BindAccess::Write ? 1u - readSide : readSide;
}

/*
 * The verbs always build a consistent Binding, but its fields are public:
 * reject a hand-built one whose type, access and target disagree.
 */
void checkBindingShape(const std::string& label, const Binding& binding) {
    const bool bufferType = binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                         || binding.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    const bool imageType = binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                        || binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    const bool buffer = binding.target == BindTarget::Buffer;

    if (buffer ? !bufferType : !imageType)
        M_FATAL("{}: binding {} has descriptor type {}, which does not fit a {}",
                label, binding.index, string_VkDescriptorType(binding.type), buffer ? "buffer" : "image");

    if ((binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) != (binding.access == BindAccess::Sample))
        M_FATAL("{}: binding {}: sample access and a combined image sampler go together, nothing else does",
                label, binding.index);

    if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        && (binding.access == BindAccess::Write || binding.access == BindAccess::ReadWrite))
        M_FATAL("{}: binding {}: a uniform buffer is read-only", label, binding.index);
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
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    bufferInfos.reserve(bindings.size());
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(bindings.size());

    for (const Binding& binding : bindings) {
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = set;
        write.dstBinding = binding.index;
        write.descriptorCount = 1;
        write.descriptorType = binding.type;

        if (binding.target == BindTarget::Buffer) {
            const GpuResources::Buffer& buffer = gpu->buffers.at(binding.resource);
            const AllocatedBuffer& side =
                buffer.sides[resolveSide(doubleBuffered, binding, buffer.doubleBuffered, mask)];

            VkDescriptorBufferInfo info = {};
            info.buffer = side.buffer;
            info.offset = 0;
            info.range = side.size;
            bufferInfos.push_back(info);
            write.pBufferInfo = &bufferInfos.back();
        } else {
            VkImageView view;
            if (binding.resource == DRAW_IMAGE_NAME) {
                view = core.drawImage.imageView;
            } else {
                const GpuResources::Image& image = gpu->images.at(binding.resource);
                view = image.sides[resolveSide(doubleBuffered, binding, image.doubleBuffered, mask)].imageView;
            }

            VkDescriptorImageInfo info = {};
            info.imageView = view;
            info.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // images stay in GENERAL during passes
            info.sampler = (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                ? gpu->sampler
                : VK_NULL_HANDLE;
            imageInfos.push_back(info);
            write.pImageInfo = &imageInfos.back();
        }

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

            if (state) {
                config.state.topology    = state->topology;
                config.state.cullMode    = state->cullMode;
                config.state.frontFace   = state->frontFace;
                config.state.blendEnable = state->blendEnable;
            }
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
    const VkPhysicalDeviceLimits& limits
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
    pass.bindings = config.bindings;

    // What a hot reload needs to rebuild the pipeline later
    pass.computePath = config.computePath;
    pass.vertexPath = config.vertexPath;
    pass.fragmentPath = config.fragmentPath;
    pass.state = config.state;

    if (config.repeat == 0)
        M_FATAL("{}: Repeat(0) is invalid, the count must be at least 1", label);
    if (config.hasPush && config.pushSize > limits.maxPushConstantsSize)
        M_FATAL("{}: push constants are {} bytes but the device allows only {}",
                label, config.pushSize, limits.maxPushConstantsSize);
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

        checkBindingShape(label, binding);

        const bool writes = binding.access == BindAccess::Write
                         || binding.access == BindAccess::ReadWrite;

        bool isDouble = false;

        if (binding.target == BindTarget::Image) {
            if (binding.resource == DRAW_IMAGE_NAME) {
                pass.usesDraw = true;
                if (graphics)
                    M_FATAL("{}: a graphics pass cannot bind '{}', it is the render target",
                            label, DRAW_IMAGE_NAME);
                if (binding.access == BindAccess::Sample) sampledSingle.insert(binding.resource);
                if (writes) writtenSingle.insert(binding.resource);
                continue;
            }

            auto it = gpu->images.find(binding.resource);
            if (it == gpu->images.end()) {
                if (gpu->buffers.count(binding.resource) > 0)
                    M_FATAL("{}: binding {} binds '{}' as an image, but it is a SimBuffer. "
                            "Use uniform, storage or storageWrite",
                            label, binding.index, binding.resource);
                M_FATAL("{}: binding {} references image '{}' but no SimImage entity has that name",
                        label, binding.index, binding.resource);
            }

            isDouble = it->second.doubleBuffered;
            if (!isDouble) {
                if (binding.access == BindAccess::Sample) sampledSingle.insert(binding.resource);
                if (writes) writtenSingle.insert(binding.resource);
                continue;
            }
        } else {
            auto it = gpu->buffers.find(binding.resource);
            if (it == gpu->buffers.end()) {
                if (binding.resource == DRAW_IMAGE_NAME || gpu->images.count(binding.resource) > 0)
                    M_FATAL("{}: binding {} binds '{}' as a buffer, but it is an image. "
                            "Use sample, read, write or readWrite",
                            label, binding.index, binding.resource);
                M_FATAL("{}: binding {} references buffer '{}' but no SimBuffer entity has that name",
                        label, binding.index, binding.resource);
            }

            const VkDeviceSize size = it->second.sides[0].size;
            if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && size > limits.maxUniformBufferRange)
                M_FATAL("{}: binding {}: buffer '{}' is {} bytes, more than the device's uniform buffer range of {}",
                        label, binding.index, binding.resource, size, limits.maxUniformBufferRange);
            if (binding.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && size > limits.maxStorageBufferRange)
                M_FATAL("{}: binding {}: buffer '{}' is {} bytes, more than the device's storage buffer range of {}",
                        label, binding.index, binding.resource, size, limits.maxStorageBufferRange);

            isDouble = it->second.doubleBuffered;
            if (!isDouble) continue;
        }

        // Double-buffered resource (image or buffer)
        if (binding.access == BindAccess::ReadWrite)
            M_FATAL("{}: binding {} uses a read-write verb on the double-buffered resource '{}'. "
                    "Use a read and a write binding (they hit different sides)",
                    label, binding.index, binding.resource);

        if (std::find(pass.doubleBuffered.begin(), pass.doubleBuffered.end(), binding.resource)
            == pass.doubleBuffered.end()) {
            pass.doubleBuffered.push_back(binding.resource);
            if (pass.doubleBuffered.size() > MAX_DOUBLE_BUFFERED_PER_PASS)
                M_FATAL("{}: binds more than {} distinct double-buffered resources",
                        label, MAX_DOUBLE_BUFFERED_PER_PASS);
        }

        if (binding.access == BindAccess::Write) {
            if (std::find(pass.written.begin(), pass.written.end(), binding.resource) != pass.written.end())
                M_FATAL("{}: the double-buffered resource '{}' is written by two bindings",
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
                pass.dispatchOnDraw = true;   // the draw image follows the window
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

            pass.localX = config.localX;
            pass.localY = config.localY;
            pass.groupX = groupCount(extent.width, config.localX);
            pass.groupY = groupCount(extent.height, config.localY);
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
            if (binding.target == BindTarget::Image && binding.resource == DRAW_IMAGE_NAME) {
                ids.push_back(0);
                continue;
            }
            const bool isDouble = isDoubleBuffered(*gpu, binding);
            ids.push_back(baseIds.at(binding.resource) + resolveSide(pass.doubleBuffered, binding, isDouble, mask));
        }

        // A graphics pass renders into the draw image whether it binds it or not
        if (graphics) ids.push_back(0);

        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }

    // Stages/accesses used when this pass is the source/destination of a barrier.
    // UNIFORM_READ is its own access bit: SHADER_READ does not cover uniform buffers.
    if (!graphics) {
        pass.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        pass.writeAccess = VK_ACCESS_2_SHADER_WRITE_BIT;
        pass.readWriteAccess = VK_ACCESS_2_SHADER_READ_BIT
                             | VK_ACCESS_2_UNIFORM_READ_BIT
                             | VK_ACCESS_2_SHADER_WRITE_BIT;
    } else {
        pass.stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                   | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                   | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        pass.writeAccess = VK_ACCESS_2_SHADER_WRITE_BIT
                         | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        pass.readWriteAccess = VK_ACCESS_2_SHADER_READ_BIT
                             | VK_ACCESS_2_UNIFORM_READ_BIT
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
    GpuResources* gpu = cmd::Resource<GpuResources>::init(key).find();   // null only if ImageSystem is not connected
    VkDevice device = core.foundation.device;

    // Opt-in hot reload: HotReload on any one entity of the Context
    auto hotRows = cmd::DynamicQuery<With<
        HotReload
    >, cmd::View<
        Include<HotReload>
    >>::init(key).atlas(entities);
    M_WARN_IF(hotRows.size() > 1,
        "{} entities carry HotReload; PassSystem uses the first and ignores the rest",
        hotRows.size()
    );
    m_hotReload = !hotRows.empty();
    m_watched.clear();
    if (m_hotReload) {
        auto [hot] = hotRows[0];
        m_pollInterval = std::chrono::milliseconds(hot.pollMs);
    }

    // Copy everything we need out of the ECS before touching Vulkan
    std::vector<PassConfig> configs = gather(key, entities);

    // Physical resource ids for barrier tracking: 0 is the draw image, then
    // one id per image side (name#0, name#1), then the same for buffers
    std::unordered_map<std::string, u32> baseIds;
    if (gpu != nullptr) {
        u32 next = 1;
        for (const auto& entry : gpu->images) {
            baseIds[entry.first] = next;
            next += entry.second.doubleBuffered ? 2 : 1;
        }
        for (const auto& entry : gpu->buffers) {
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
        m_passes.push_back(analyse(config, core, gpu, baseIds, deviceProps.limits));

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

    // Hot reload bookkeeping: one WatchedFile per distinct path, so a file
    // shared by several passes is stat'd once per poll
    std::unordered_map<std::string, u32> watchIndex;
    auto watchPath = [&](const std::string& path) -> u32 {
        auto found = watchIndex.find(path);
        if (found != watchIndex.end()) return found->second;

        // On error this is file_time_type::min(): the first poll then sees a
        // change and reloads an unchanged file once, which is harmless
        std::error_code ec;
        WatchedFile file;
        file.path = path;
        file.observed = std::filesystem::last_write_time(path, ec);
        file.accepted = file.observed;

        const u32 index = u32(m_watched.size());
        m_watched.push_back(std::move(file));
        watchIndex.emplace(path, index);
        return index;
    };

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

        // Stat the files BEFORE reading them: a write that lands in between is
        // then seen as a change by the first poll instead of being missed
        if (m_hotReload) {
            if (graphics)
                pass.watched = { watchPath(config.vertexPath), watchPath(config.fragmentPath) };
            else
                pass.watched = { watchPath(config.computePath) };
            pass.builtVersions.assign(pass.watched.size(), 0u);
        }

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

        pass.pipeline = std::make_shared<PipelineHolder>();
        pass.pipeline->handle = built.pipeline;
        pass.layout = built.layout;

        // The layout never changes, so its deletor captures it by value. The
        // pipeline can be swapped by a hot reload, so its deletor captures the
        // holder (never `this`) and destroys whatever is current at flush time.
        // Pushed second, so the pipeline dies before its layout (LIFO).
        core.dqueue.push([device, layout = built.layout]() {
            vkDestroyPipelineLayout(device, layout, nullptr);
        });
        core.dqueue.push([device, holder = pass.pipeline]() {
            if (holder->handle != VK_NULL_HANDLE) vkDestroyPipeline(device, holder->handle, nullptr);
            holder->handle = VK_NULL_HANDLE;
        });
    }

    m_lastPoll = std::chrono::steady_clock::now();
    m_generation = core.swapchainGeneration;
}

/* -------------------------------------------------------------------- */
/* ------------------------------ Refresh ------------------------------ */
/* -------------------------------------------------------------------- */

/*
 * The draw image was recreated (window resize). Called at the start of tick,
 * after FrameBegin has waited for the device to go idle, so no descriptor set
 * is in use and no command buffer that uses one is pending. The pipelines do
 * not depend on the draw extent (dynamic viewport/scissor, fixed format).
 */
void PassSystem::refresh(const RenderCore& core, const GpuResources* gpu) {
    for (CompiledPass& pass : m_passes) {
        if (pass.dispatchOnDraw) {
            pass.groupX = groupCount(core.drawImage.imageExtent.width, pass.localX);
            pass.groupY = groupCount(core.drawImage.imageExtent.height, pass.localY);
        }

        if (!pass.usesDraw) continue;
        for (u32 mask = 0; mask < pass.sets.size(); mask++) {
            writeDescriptorSet(
                core.foundation.device, pass.sets[mask], pass.bindings, pass.doubleBuffered, mask, core, gpu
            );
        }
    }
}

/* -------------------------------------------------------------------- */
/* ---------------------------- Hot reload ----------------------------- */
/* -------------------------------------------------------------------- */

/*
 * Called at the start of tick, before anything is recorded, and only when a
 * HotReload component exists.
 *
 * 1. At most every pollInterval, stat each distinct file once. A new mtime is
 *    only ACCEPTED (version bumped) when it is the same on two polls in a
 *    row: a tool may still be writing the file.
 * 2. A pass is stale when a file of it has a version it was not built from.
 *    It waits while any of its files is still settling, so a vertex shader
 *    that is ready is not paired with a half-written fragment shader.
 * 3. If anything is stale: wait for the device (the other frame slot may
 *    still be executing the old pipelines), build the new pipelines against
 *    the existing layouts, and only when one succeeds swap it in and destroy
 *    the old one. A failure logs and keeps the old pipeline.
 */
void PassSystem::pollShaders(const RenderCore& core) {
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastPoll < m_pollInterval) return;
    m_lastPoll = now;

    for (WatchedFile& file : m_watched) {
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(file.path, ec);
        if (ec) continue;   // gone for a moment (a tool replacing it): look again next poll

        if (mtime != file.observed) {
            file.observed = mtime;
        } else if (file.observed != file.accepted) {
            file.accepted = file.observed;
            file.version++;
        }
    }

    std::vector<CompiledPass*> stale;
    for (CompiledPass& pass : m_passes) {
        bool changed = false;
        bool settling = false;
        for (size_t k = 0; k < pass.watched.size(); k++) {
            const WatchedFile& file = m_watched[pass.watched[k]];
            if (file.version != pass.builtVersions[k]) changed = true;
            if (file.observed != file.accepted) settling = true;
        }
        if (changed && !settling) stale.push_back(&pass);
    }
    if (stale.empty()) return;

    VkDevice device = core.foundation.device;
    VK_CHECK(vkDeviceWaitIdle(device));

    utils::interface::PipelineBuilder builder(device);
    for (CompiledPass* pass : stale) {
        // Consume the versions first: a broken file is reported once and not
        // retried until it changes again
        for (size_t k = 0; k < pass->watched.size(); k++)
            pass->builtVersions[k] = m_watched[pass->watched[k]].version;

        VkPipeline fresh = pass->kind == Kind::Graphics
            ? builder.tryBuildGraphics(
                pass->vertexPath, pass->fragmentPath, pass->layout,
                pass->state, core.drawImage.imageFormat)
            : builder.tryBuildCompute(pass->computePath, pass->layout);

        if (fresh == VK_NULL_HANDLE) {
            M_ERROR("{}: shader reload failed, keeping the previous pipeline", pass->label);
            continue;
        }

        vkDestroyPipeline(device, pass->pipeline->handle, nullptr);
        pass->pipeline->handle = fresh;
        M_INFO("{}: shaders reloaded", pass->label);
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
 * rendering, also in GENERAL. Buffers have no layout.
 *
 * Barriers (linear, no frame graph): remember which physical images and buffers
 * have been touched since the last barrier. When a pass touches one of them
 * again, emit one memory barrier covering everything pending, then carry on.
 */
void PassSystem::tick(const Key<key::READ>& key) {
    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    GpuResources* gpu = cmd::Resource<GpuResources>::init(key).find();   // null only if ImageSystem is not connected
    VkCommandBuffer cmdBuf = core.frame.cmd;

    // The swapchain was rebuilt since the last frame: redo what depends on the draw image
    if (core.swapchainGeneration != m_generation) {
        refresh(core, gpu);
        m_generation = core.swapchainGeneration;
    }

    // Opt-in: swap in rebuilt pipelines before anything is recorded this frame
    if (m_hotReload) pollShaders(core);

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
        const VkPipeline pipeline = pass.pipeline->handle;

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
                mask |= readSideOf(*gpu, pass.doubleBuffered[j]) << j;

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
                vkCmdBindPipeline(cmdBuf, bindPoint, pipeline);
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
                vkCmdBindPipeline(cmdBuf, bindPoint, pipeline);
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
                readSideOf(*gpu, name) ^= 1u;
        }

        if (pass.once) pass.onceDone = true;
    }
}

}
