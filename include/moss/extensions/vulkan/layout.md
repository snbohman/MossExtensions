window
shaders

thats it. connected shaders (compute, frag, vert) and hot reloading, and
ergonomic API for connecting stuff.

using VKH.

```cpp
class ShaderCtx : public moss::Context {
public:
    void init(moss::Mirror& mirror) override {
        mirror
            .create() // Shader
                .attach<mvk::ShaderTag>() // Categorization/Identification for shader
                .attach<mvk::VertexShader>("shaders/vert.glsl")
                .attach<mvk::FragmentShader>("shaders/frag.glsl")
                .attach<mvk::Bindings>({
                    .ubos = { ... }
                    .samplers = { ... }
                })
            .create()
                .attach<mvk::ShaderTag>() // Categorization/Identification for shader
                .attach<mvk::ComputeShader>("shaders/compute.glsl");
    };
};

class RenderContext : public moss::Context {
public:
    void init(moss::Mirror& mirror) override {
        mirror.create()
            .attach<mvk::Window>("HELLO", 800, 800, 60)
            .connect<mvk::Renderer>();
    };
};
```

Follow triangle example...

1. Instance creation

Use vkb::InstanceBuilder to request a Vulkan instance: set API version (1.3,
for dynamic rendering), enable validation layers (debug builds only), enable
the debug messenger, and request the extensions GLFW says it needs
(glfwGetRequiredInstanceExtensions). This gives you back a vkb::Instance
wrapping VkInstance + debug messenger handle.

2. Window + surface

Create the GLFW window (remember GLFW_CLIENT_API = GLFW_NO_API, since GLFW
defaults to OpenGL context creation otherwise). Immediately create the
VkSurfaceKHR from it — this has to exist before physical device selection,
since device selection needs to check presentation support against a real
surface.

3. Physical device selection

Use vkb::PhysicalDeviceSelector, feeding it the instance and surface. Set your
required Vulkan 1.3 features here — specifically enable dynamicRendering and
synchronization2 in the VkPhysicalDeviceVulkan13Features struct you pass in.
Let it auto-select a suitable GPU (or filter to discrete GPU preference).

4. Logical device creation

vkb::DeviceBuilder from the selected physical device. This gives you the
VkDevice plus queue handles — pull out the graphics queue and present queue
(and their family indices) via the builder's queue-getter functions, which
handle the annoying case where they're different queue families.

5. Swapchain creation

vkb::SwapchainBuilder, specifying present mode (FIFO for vsync, mailbox if you
want uncapped), format preference (sRGB usually), and image usage flags
(COLOR_ATTACHMENT at minimum). Extract the swapchain images and image views
from the result — you'll rebuild this whole object on window resize.

6. VMA allocator setup

Initialize VmaAllocator right after device creation, feeding it the instance,
physical device, device, and Vulkan API version/function pointers. This becomes
your single allocation entry point for every buffer and image from here on —
including your UBOs.

7. Command pool + command buffers

One VkCommandPool per frame-in-flight (or one pool with per-frame buffers,
depending on how you structure frame overlap), tied to the graphics queue
family index. Allocate primary command buffers from it.

8. Sync objects

Per frame-in-flight: one semaphore for "image available," one for "render
finished," one fence for "frame complete" (CPU-GPU sync so you don't overwrite
a command buffer still in flight).

9. Descriptor infrastructure

Create your own VkDescriptorPool sized for your engine's needs (UBOs, samplers,
storage buffers), and a separate VkDescriptorPool for ImGui — don't share
pools, ImGui wants its own headroom and lifecycle.

10. Shader/pipeline layer (your own code, not vk-bootstrap's job)

This is where your mvk::Shader, VertexShader, Bindings component design comes
in: load SPIR-V, build VkShaderModules, build VkDescriptorSetLayout from your
bindings description, build VkPipelineLayout, build the VkPipeline itself using
dynamic rendering (VkPipelineRenderingCreateInfo referencing your swapchain's
color format instead of a VkRenderPass).

11. ImGui initialization

Init imgui_impl_glfw against your GLFW window, then imgui_impl_vulkan with an
ImGui_ImplVulkan_InitInfo populated from your instance/device/queue/allocator,
UseDynamicRendering = true, and the color attachment format matching your
swapchain. Upload fonts once via the backend's font upload call.

12. Per-frame loop

Wait on the frame's fence → acquire swapchain image → begin command buffer →
begin dynamic rendering (vkCmdBeginRendering) targeting the acquired image →
your ECS render system draws → end scene rendering → start ImGui frame, draw
your bindings-editor UI, render ImGui's draw data into the same command buffer
→ end rendering → submit with sync objects → present.

13. Resize handling

On window resize/minimize, destroy and rebuild the swapchain
(vkb::SwapchainBuilder again, reusing the old swapchain handle as old_swapchain
for a smoother transition), and recreate anything tied to swapchain image
count/format if needed.
