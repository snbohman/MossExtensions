#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"

#include <concepts>
#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/*
 * A pass is an entity. There is no Pass marker, no Shader<Kind> tag and no
 * PassOrder: PassSystem classifies an entity by which shader components it
 * carries, and pass order is the order of the .create() calls.
 *
 *   ComputeShader + Dispatch          -> compute pass
 *   VertexShader + FragmentShader     -> graphics pass (renders into the draw image)
 *
 * Optional on any pass: Bind, Repeat, Once, PushConstants.
 * Graphics only: PipelineState, DrawCall. Compute only: Dispatch.
 * Optional on any ONE entity of the Context (not a pass): HotReload.
 */

namespace moss::extensions::vulkan {

/* ------------------------------------------------------------------------ */
/* ------------------------------ Shader sources -------------------------- */
/* ------------------------------------------------------------------------ */

enum class ShaderStage { Compute, Vertex, Fragment };

// Path to a precompiled SPIR-V file for one stage. The shaders are yours.
template<ShaderStage S>
struct ShaderSource : public Component {
    ShaderSource(std::string p) : path(std::move(p)) { }
    std::string path;
};

using ComputeShader  = ShaderSource<ShaderStage::Compute>;
using VertexShader   = ShaderSource<ShaderStage::Vertex>;
using FragmentShader = ShaderSource<ShaderStage::Fragment>;

/* ------------------------------------------------------------------------ */
/* -------------------------------- Bindings ------------------------------ */
/* ------------------------------------------------------------------------ */

enum class BindAccess { Sample, Read, Write, ReadWrite };

// What `resource` names: a SimImage (or "draw") or a SimBuffer
enum class BindTarget { Image, Buffer };

/*
 * One descriptor slot plus WHAT goes in it. Build it with the verbs below,
 * which set `type`, `access` and `target`. `index` equals layout(binding = N)
 * in your shader: nothing is inferred. `resource` is a SimImage name,
 * DRAW_IMAGE_NAME ("draw") or a SimBuffer name; a graphics pass may not bind
 * "draw" (it is the render target).
 */
struct Binding {
    u32 index;
    VkDescriptorType type;
    BindAccess access;
    std::string resource;
    BindTarget target = BindTarget::Image;
};

/*
 * Resolution (image descriptor layout is always GENERAL). Images and buffers
 * resolve the same way; "side" only matters when the resource is DoubleBuffered:
 *
 *   verb              descriptor type          single resource   double-buffered
 *   sample            COMBINED_IMAGE_SAMPLER   the image         read side
 *   read              STORAGE_IMAGE            the image         read side
 *   write             STORAGE_IMAGE            the image         write side (the other one),
 *                                                                the sides swap after the iteration
 *   readWrite         STORAGE_IMAGE            the image         error
 *   uniform           UNIFORM_BUFFER           the buffer        read side
 *   storage           STORAGE_BUFFER           the buffer        read side
 *   storageWrite      STORAGE_BUFFER           the buffer        write side, sides swap afterwards
 *   storageReadWrite  STORAGE_BUFFER           the buffer        error
 *
 * The verb only picks the descriptor type and the double-buffer side. What the
 * shader does with the binding is the shader's business. Image verbs take a
 * SimImage name, buffer verbs a SimBuffer name.
 *
 * Call these qualified (vk::read, vk::write): the unqualified names can clash
 * with POSIX read(2) and write(2) under a using-directive.
 */
inline Binding sample(u32 index, std::string image) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BindAccess::Sample, std::move(image), BindTarget::Image };
}
inline Binding read(u32 index, std::string image) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BindAccess::Read, std::move(image), BindTarget::Image };
}
inline Binding write(u32 index, std::string image) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BindAccess::Write, std::move(image), BindTarget::Image };
}
inline Binding readWrite(u32 index, std::string image) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BindAccess::ReadWrite, std::move(image), BindTarget::Image };
}

inline Binding uniform(u32 index, std::string buffer) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BindAccess::Read, std::move(buffer), BindTarget::Buffer };
}
inline Binding storage(u32 index, std::string buffer) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BindAccess::Read, std::move(buffer), BindTarget::Buffer };
}
inline Binding storageWrite(u32 index, std::string buffer) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BindAccess::Write, std::move(buffer), BindTarget::Buffer };
}
inline Binding storageReadWrite(u32 index, std::string buffer) {
    return Binding{ index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BindAccess::ReadWrite, std::move(buffer), BindTarget::Buffer };
}

/*
 * Variadic instead of a braced list (attach cannot forward those). The
 * constraint keeps it from hijacking copy construction: a copy would deduce
 * B = Bind, which is not Binding.
 */
struct Bind : public Component {
    Bind() = default;

    template<typename... B>
    requires(sizeof...(B) > 0 && (std::same_as<B, Binding> && ...))
    explicit Bind(B... b) {
        bindings.reserve(sizeof...(B));
        (bindings.push_back(std::move(b)), ...);
    }

    std::vector<Binding> bindings;
};

/* ------------------------------------------------------------------------ */
/* ---------------------------- Compute dispatch -------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Compute passes, required. Two forms:
 *
 *   Dispatch(gx, gy, gz)                  explicit WORKGROUP counts
 *   Dispatch("image", localX, localY)     groups = ceil(extent / local), z = 1
 *
 * The image form only works if the shader declares the same local_size_x and
 * local_size_y as the defaults (16 x 16) or the values you pass: you keep them
 * in sync. The image is a SimImage name or "draw". With "draw" the group counts
 * follow the window when it is resized; the explicit form never changes.
 */
struct Dispatch : public Component {
    Dispatch(u32 gx, u32 gy = 1, u32 gz = 1)
        : fromImage(false), groupX(gx), groupY(gy), groupZ(gz), localX(16), localY(16) { }

    Dispatch(std::string img, u32 lx = 16, u32 ly = 16)
        : fromImage(true), groupX(1), groupY(1), groupZ(1),
          image(std::move(img)), localX(lx), localY(ly) { }

    bool fromImage;
    u32 groupX, groupY, groupZ;   // explicit form
    std::string image;            // image form
    u32 localX, localY;           // image form
};

/* ------------------------------------------------------------------------ */
/* ---------------------------- Graphics settings ------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Graphics passes, optional. Wraps the plain GraphicsState so the generic
 * pipeline builder does not depend on components. Defaults describe the
 * fullscreen triangle: no culling, no blending. The colour format is always
 * the draw image's.
 */
struct PipelineState : public Component {
    PipelineState(
        VkPrimitiveTopology t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VkCullModeFlags c = VK_CULL_MODE_NONE,
        VkFrontFace f = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        bool blend = false
    ) {
        state.topology = t;
        state.cullMode = c;
        state.frontFace = f;
        state.blendEnable = blend;
    }

    utils::interface::GraphicsState state;
};

// Graphics passes, optional. Defaults are the fullscreen triangle.
struct DrawCall : public Component {
    DrawCall(u32 vertices = 3, u32 instances = 1)
        : vertexCount(vertices), instanceCount(instances) { }

    u32 vertexCount;
    u32 instanceCount;
};

/* ------------------------------------------------------------------------ */
/* ------------------------------ Optional parts -------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Run the pass `count` times back to back (count >= 1). Each iteration that
 * writes a double-buffered resource swaps its sides afterwards.
 */
struct Repeat : public Component {
    explicit Repeat(u32 c) : count(c) { }
    u32 count;
};

// Run the pass only on the first frame (seeding an image, for example).
struct Once : public Component { };

/*
 * Push constants, up to 128 bytes (and the device's maxPushConstantsSize).
 * Stages come from the pass kind (compute stage for compute passes, vertex and
 * fragment for graphics passes), offset 0. The block is copied in from a
 * trivially copyable struct whose size is a multiple of 4; edit it later with
 * as<T>() from any system that runs before PassSystem:
 *
 *     .attach<vk::PushConstants>(SplatParams{ 0.5f, 0.5f, 0.016f, 0.05f })
 *     pc.as<SplatParams>().x = ...;
 */
struct PushConstants : public Component {
    static constexpr u32 MAX_SIZE = 128;

    // The !same_as keeps this from hijacking copy construction
    template<typename T>
    requires(std::is_trivially_copyable_v<T> && sizeof(T) <= MAX_SIZE && sizeof(T) % 4 == 0
             && alignof(T) <= 16 && !std::same_as<std::remove_cvref_t<T>, PushConstants>)
    explicit PushConstants(const T& initial) : size(u32(sizeof(T))) {
        std::memcpy(data, &initial, sizeof(T));
    }

    // Aborts if sizeof(T) differs from the size the block was created with
    template<typename T>
    T& as() {
        static_assert(std::is_trivially_copyable_v<T> && alignof(T) <= 16,
            "PushConstants::as<T>: T must be trivially copyable with alignment <= 16");
        if (sizeof(T) != size)
            M_FATAL("PushConstants::as<T>: sizeof(T) is {} but the block is {} bytes", sizeof(T), size);
        return *std::launder(reinterpret_cast<T*>(data));
    }

    u32 size = 0;
    alignas(16) std::byte data[MAX_SIZE] = {};
};

/* ------------------------------------------------------------------------ */
/* ------------------------------- Hot reload ----------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Opt-in shader hot reload. Put it on any ONE entity of the Context (PassSystem
 * warns and uses the first if several carry it). Absent means off: nothing is
 * polled and nothing is stat'd.
 *
 *     .attach<vk::HotReload>()        // poll every 500 ms
 *     .attach<vk::HotReload>(250u)    // poll every 250 ms (0 = every frame)
 *
 * What it does: at most every `pollMs`, PassSystem checks the modification time
 * of the SPIR-V file of every pass (a file shared by several passes is checked
 * once). A change is only acted on once the time was the same on two polls in
 * a row, so a tool that is still writing the file is waited for: expect the new
 * shader 1-2 poll intervals after the file is written. Then the pass's pipeline
 * is rebuilt from the files and swapped in at the start of the next recording.
 *
 * What it does NOT do: it replaces the PIPELINE only. The descriptor set
 * layout, the sets, the push-constant range and the bindings come from the
 * components, not from the shader, and nothing is reflected. A shader whose
 * interface changed (other bindings, other push block) needs a restart; the
 * validation layers will say so if you forget.
 *
 * Failure never aborts: a missing, truncated or invalid file, or a pipeline
 * that fails to build, logs an error and the old pipeline keeps running. The
 * file is not retried until it changes again.
 */
struct HotReload : public Component {
    explicit HotReload(u32 poll = 500) : pollMs(poll) { }

    u32 pollMs;
};

}
