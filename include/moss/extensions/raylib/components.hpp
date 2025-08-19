#pragma once

#include <moss/moss.hpp>
#include <raylib.h>


namespace moss::extensions::raylib {

struct Window : public Component {
    const char* title = "Moss | Raylib";
    i32 width = 800;
    i32 height = 800;
    i32 targetFPS = 60;
};

struct Transform : public Component {
    i32 x, y;
};

struct Albedo : public Component {
    f32 r, g, b;
};

struct RectShape : public Component {
    i32 x, y;
};

struct Renderable : public Component {
    virtual void draw() = 0;
};

struct RectRenderable : public Renderable {
    void draw() override {
        DrawRectangle(
            pos.x, pos.y,
            shape.x, shape.y,
            ColorFromNormalized({
                albedo.r, albedo.g, albedo.b
            })
        );
    }

    RectShape shape;
    Transform pos;
    Albedo albedo;
};
}
