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
    Transform(i32 _x, i32 _y) : x(_x), y(_y) { }
    i32 x, y;
};

struct Albedo : public Component {
    Albedo(f32 _r, f32 _g, f32 _b) : r(_r), g(_g), b(_b)  { }
    f32 r, g, b;
};

struct RectShape : public Component {
    RectShape(i32 _x, i32 _y) : x(_x), y(_y) { }
    i32 x, y;
};

struct Renderable : public Component {
    virtual void draw() = 0;
};

struct RectRenderable : public Renderable {
    RectRenderable(
        const Transform& t,
        const RectShape& s,
        const Albedo& a
    ) : shape(s), transform(t), albedo(a) { }

    void draw() override {
        DrawRectangle(
            transform.x, transform.y,
            shape.x, shape.y,
            ColorFromNormalized({
                albedo.r, albedo.g, albedo.b, 1
            })
        );
    }

    Transform transform;
    RectShape shape;
    Albedo albedo;
};
}
