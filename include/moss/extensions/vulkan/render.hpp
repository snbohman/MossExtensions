#pragma once

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

        SetTraceLogLevel(LOG_WARNING);
        InitWindow(window.width, window.height, window.title);
        SetTargetFPS(window.targetFPS);
    }

    void tick(const Key<key::READ>& key) override {
        if (WindowShouldClose()) {
            commands::Quit::init(key).quit();
        }

        auto atlas = commands::Query<
            With<RectRenderable>,
            commands::View<
                Include<RectRenderable>
            >
        >::init(key).atlas();

        BeginDrawing();
        ClearBackground(RAYWHITE);

        for (const auto& [rect] : atlas) {
            rect.draw();
        }

        EndDrawing();
    }

    void exit() override {
        CloseWindow();
    }

};

}
