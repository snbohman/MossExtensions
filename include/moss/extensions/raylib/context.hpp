#pragma once

#include <moss/moss.hpp>
#include <moss/extensions/raylib/components.hpp>
#include <moss/extensions/raylib/render.hpp>


namespace moss::extensions::raylib {

class Renderer : public moss::Context {
public:
    void init(moss::Mirror& mirror) override {
        mirror
            .create()
                .attach<Window>()
            .connect<Render>();
    }
};

} // moss::extensions::raylib
