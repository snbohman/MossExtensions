#pragma once

#include <moss/moss.hpp>
#include <iostream>


namespace moss::extensions::raylib {

struct Window : public moss::Component {
    const char* title;
    int width;
    int height;
    int targetFPS;
};

struct Renderable : public moss::Component {
    virtual void draw() = 0;
};

struct RectRenderable : public Renderable {
    void draw() override {
        std::cout << "DRAAAW" << std::endl;
    }
};

}
