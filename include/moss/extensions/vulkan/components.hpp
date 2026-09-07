#pragma once

#include <moss/moss.hpp>


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
    WindowSettings(const char* t, i32 w, i32 h, i32 fps) : title(t), width(w), height(h), targetFPS(fps) { }
    const char* title;
    i32 width;
    i32 height;
    i32 targetFPS;
    bool resize;
};

class ShaderTag : public moss::Component { };

class VertexShader : public moss::Component {
    const char* path;
};

class FragmentShader : public moss::Component {
    const char* path;
};

class ComputeShader : public moss::Component {
    const char* path;
};

class Bindings : public moss::Component {
    // ubos
    // ssbos
    // ...
};



}
