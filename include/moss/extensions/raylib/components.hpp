#pragma once

#include <moss/moss.hpp>
#include <raylib.h>


namespace moss::extensions::raylib {

struct Window : public Component {
    Window(const char* t, i32 w, i32 h, i32 fps) : title(t), width(w), height(h), targetFPS(fps) { }
    const char* title;
    i32 width;
    i32 height;
    i32 targetFPS;
};

struct Transform : public Component {
    Transform(i32 _x, i32 _y) : x(_x), y(_y) { }
    f32 x, y;
};

struct Albedo : public Component {
    Albedo(f32 _r, f32 _g, f32 _b, f32 _a) : r(_r), g(_g), b(_b), a(_a)  { }
    f32 r, g, b, a;
};

struct RectShape : public Component {
    RectShape(i32 _x, i32 _y) : x(_x), y(_y) { }
    f32 x, y;
};

struct Renderable : public Component {
    virtual void draw() = 0;
};

struct RectRenderable : public Renderable {
    RectRenderable(
        const Transform& t,
        const RectShape& s,
        const Albedo& a
    ) : transform(t), shape(s), albedo(a) { }

    void draw() override {
        DrawRectangle(
            transform.x, transform.y,
            shape.x, shape.y,
            ColorFromNormalized({
                albedo.r, albedo.g, albedo.b, albedo.a
            })
        );
    }

    Transform transform;
    RectShape shape;
    Albedo albedo;
};
}
