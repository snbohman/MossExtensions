#pragma once

#include <moss/extensions/vulkan/meta.hpp>
#include <vulkan/vulkan_core.h>


// mirror
//     .create() // Shader
//         .attach<mvk::ShaderTag>() // Categorization/Identification for shader
//         .attach<mvk::VertexShader>("shaders/vert.glsl")
//         .attach<mvk::FragmentShader>("shaders/frag.glsl")
//         .attach<mvk::Bindings>({
//             .ubos = { ... }
//             .samplers = { ... }
//         })
//     .create()
//         .attach<mvk::ShaderTag>() // Categorization/Identification for shader
//         .attach<mvk::ComputeShader>("shaders/compute.glsl")

namespace moss::extensions::vulkan {

struct RenderSettings : public Component {
    RenderSettings(bool useVL) : validationLayers(useVL) { }
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

struct Binding {
    u32 index;
    VkDescriptorType type;
};

class Bindings : public moss::Component {
public:
    Bindings(std::initializer_list<Binding> bindings) {
        for (Binding binding : bindings) {
            this->bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding = binding.index,
                .descriptorType = binding.type,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = nullptr
            });
        }
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

}
