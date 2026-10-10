#pragma once

#include <moss/extensions/vulkan/meta.hpp>
#include <vulkan/vulkan_core.h>

#include <string>
#include <utility>
#include <vector>

// Example (see bottom of file for a complete fluid-sim style setup):
//
// mirror
//     .create() // Compute pass
//         .attach<mvk::Pass>()
//         .attach<mvk::Shader<mvk::PipelineKind::Compute>>()
//         .attach<mvk::PassOrder>(0)
//         .attach<mvk::ComputeShader>("shaders/diffuse.comp.spv")
//         .attach<mvk::GridDispatch>(80, 45, 1)
//         .attach<mvk::Bindings>(std::vector<mvk::Binding>{
//             {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "fieldA"},
//             {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "fieldB"}
//         })
//     .create() // Display pass
//         .attach<mvk::Pass>()
//         .attach<mvk::Shader<mvk::PipelineKind::Graphics>>()
//         .attach<mvk::PassOrder>(100)
//         .attach<mvk::VertexShader>("shaders/fullscreen.vert.spv")
//         .attach<mvk::FragmentShader>("shaders/display.frag.spv")
//         .attach<mvk::PipelineState>()
//         .attach<mvk::DrawCall>()
//         .attach<mvk::Bindings>(std::vector<mvk::Binding>{
//             {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "fieldA"}
//         })

namespace moss::extensions::vulkan {

struct RendererSettings : public Component {
    RendererSettings(bool useVL) : validationLayers(useVL) { }
    bool validationLayers;
};

struct WindowSettings : public Component {
    WindowSettings(const char* t, i32 w, i32 h, i32 fps, bool r)
        : title(t), width(w), height(h), targetFPS(fps), resize(r) { }
    const char* title;
    u32 width;
    u32 height;
    u32 targetFPS;
    bool resize;
};

/* ------------------------------------------------------------------------ */
/* ------------------------- Resource binding data ------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * One descriptor slot plus WHAT goes in it.
 *
 * Images are referenced by NAME, not by handle: entities are built before the
 * Vulkan device exists, so the Renderer resolves the name at descriptor-write
 * time. Names resolve to either
 *   - "" or "draw"   -> the renderer's draw image (m_drawImage), or
 *   - a SimImage name -> an image created from a SimImage entity.
 * Buffers are carried as real handles (they are not renderer-owned yet).
 *
 * Supported types: STORAGE_IMAGE, COMBINED_IMAGE_SAMPLER (shared linear/clamp
 * sampler owned by the renderer), UNIFORM_BUFFER, STORAGE_BUFFER.
 *
 * Aggregate with defaults on purpose: the old `{index, type}` form still works.
 */
struct Binding {
    u32 index;
    VkDescriptorType type;
    std::string resource = "";
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

class Bindings : public moss::Component {
public:
    Bindings(std::vector<Binding> binds) : resources(std::move(binds)) {
        this->bindings.reserve(resources.size());
        for (const Binding& b : resources) {
            this->bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding = b.index,
                .descriptorType = b.type,
                .descriptorCount = 1,
                // Stage flags are OR'd in per pipeline kind when the layout is built
                .stageFlags = 0,
                .pImmutableSamplers = nullptr
            });
        }
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings; // layout side (same order as resources)
    std::vector<Binding> resources;                     // resource side
};

/*
 * Creates a storage+sampled image the size of the draw image, named so that
 * Bindings / PingPong can refer to it. Zero-initialised once, then persistent
 * across frames (this is where sim state lives).
 */
struct SimImage : public Component {
    SimImage(std::string n, VkFormat f = VK_FORMAT_R16G16B16A16_SFLOAT)
        : name(std::move(n)), format(f) { }
    std::string name;
    VkFormat format;
};

/* ------------------------------------------------------------------------ */
/* ------------------------- Pipeline kind components --------------------- */
/* ------------------------------------------------------------------------ */

enum class PipelineKind { Compute, Graphics };

// Tag: categorises an entity's pipeline as compute or graphics.
template<PipelineKind K>
struct Shader : public Component {
    static constexpr PipelineKind kind = K;
};

enum class ShaderStage { Compute, Vertex, Fragment };

// Path to a precompiled SPIR-V file for one stage.
template<ShaderStage S>
struct ShaderSource : public Component {
    ShaderSource(std::string p) : path(std::move(p)) { }
    std::string path;
};

using ComputeShader  = ShaderSource<ShaderStage::Compute>;
using VertexShader   = ShaderSource<ShaderStage::Vertex>;
using FragmentShader = ShaderSource<ShaderStage::Fragment>;

/*
 * Minimal fixed-function state for the graphics pipeline. Defaults describe
 * the fullscreen triangle: no vertex input (gl_VertexIndex), no depth test,
 * no culling, no blending. The colour format is always the draw image's.
 */
struct PipelineState : public Component {
    PipelineState(
        VkPrimitiveTopology t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VkCullModeFlags c = VK_CULL_MODE_NONE,
        VkFrontFace f = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        bool blend = false
    ) : topology(t), cullMode(c), frontFace(f), blendEnable(blend) { }

    VkPrimitiveTopology topology;
    VkCullModeFlags cullMode;
    VkFrontFace frontFace;
    bool blendEnable;
};

/* ------------------------------------------------------------------------ */
/* ---------------------------- Pass components --------------------------- */
/* ------------------------------------------------------------------------ */

// Tag: this entity is a pass the renderer should run each frame.
struct Pass : public Component { };

// Explicit linear ordering. Must be unique per pass (it is also the join key
// for the optional PassRepeat / PingPong components).
struct PassOrder : public Component {
    PassOrder(int o) : order(o) { }
    int order;
};

// Compute passes: WORKGROUP COUNTS (not threads). e.g. ceil(width / local_size_x).
struct GridDispatch : public Component {
    GridDispatch(u32 gx, u32 gy = 1, u32 gz = 1) : x(gx), y(gy), z(gz) { }
    u32 x;
    u32 y;
    u32 z;
};

// Graphics pass: defaults are the fullscreen triangle.
struct DrawCall : public Component {
    DrawCall(u32 vertices = 3, u32 instances = 1)
        : vertexCount(vertices), instanceCount(instances) { }
    u32 vertexCount;
    u32 instanceCount;
};

// OPTIONAL: run the pass `count` times back to back (e.g. pressure solve).
// With PingPong, use an EVEN count so the result ends up back in the first image.
struct PassRepeat : public Component {
    PassRepeat(u32 n) : count(n) { }
    u32 count;
};

// OPTIONAL: on odd PassRepeat iterations, any binding that names `a` is
// pointed at `b` and vice versa.
struct PingPong : public Component {
    PingPong(std::string first, std::string second)
        : a(std::move(first)), b(std::move(second)) { }
    std::string a;
    std::string b;
};

}
