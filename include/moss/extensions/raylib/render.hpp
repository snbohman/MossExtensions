#pragma once

#include "moss/commands/quit.hpp"
#include <moss/extensions/raylib/components.hpp>
#include <moss/moss.hpp>
#include <raylib.h>


namespace moss::extensions::raylib {

class Render : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override {
        auto [window] = commands::DynamicQuery<With<
            Window        
        >>::init(key).pool(entities);

        InitWindow(window.width, window.height, window.title);
        SetTargetFPS(window.targetFPS);
    }

    void tick(const Key<key::READ>& key) override {
        if (WindowShouldClose()) {
            commands::Quit::init(key).quit();
        }

        auto renderables = commands::Query<
            With<Renderable>,
            commands::View<
                Include<Renderable>
            >
        >::init(key).atlas();

        for (const auto& [renderable] : renderables) {
            renderable.draw();
        }
}

private:
    

};

}
