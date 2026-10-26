#pragma once

#include "moss/extensions/imgui/meta.hpp"
#include "moss/extensions/imgui/components.hpp"

namespace moss::extensions::imgui {

/*
 * ImGui attachment for the Vulkan extension. Two systems, mirroring
 * FrameBegin/FrameEnd. Your UI systems are ordinary moss systems that call the
 * raw ImGui API between them; there are no widget components and nothing is
 * inferred.
 *
 *     mirror.connect<
 *         vk::Core, vk::FrameBegin, vk::ImageSystem,
 *         ig::Begin,
 *         MyUiSystem,            // ImGui::Begin(...) ... ImGui::End()
 *         MyParamsSystem,        // copies edited values into PushConstants
 *         vk::PassSystem,
 *         ig::End,
 *         vk::FrameEnd
 *     >();
 *
 * Values live in a plain component you own. The UI system edits it in place
 * (ImGui::SliderFloat(..., &params.x, ...)); a system placed after the UI and
 * before PassSystem copies it into the PushConstants block, so an edit reaches
 * the shader in the same frame.
 *
 * Input: ImGui sees the mouse and keyboard through GLFW callbacks that it
 * chains onto the window. A system that reads the mouse or keys itself checks
 * ImGui::GetIO().WantCaptureMouse / WantCaptureKeyboard first, or a click on a
 * slider also moves the cylinder.
 *
 * Screenshots (readback) must be recorded between PassSystem and ig::End, so
 * they do not contain the UI.
 */

/*
 * build: creates the ImGui context, the GLFW and Vulkan backends (dynamic
 * rendering, colour format of the draw image, own descriptor pool) and pushes
 * their teardown to core.dqueue. Needs RenderCore, so connect it after Core.
 * tick: backend NewFrame, then ImGui::NewFrame.
 *
 * It has no exit: everything is destroyed by Core::exit through core.dqueue,
 * before the window goes away.
 */
class Begin : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;

private:
    bool m_dockspace = false;
};

/*
 * tick: ImGui::Render, then records the draw data into the draw image
 * (syncAll, LOAD/STORE rendering, the image is left in GENERAL). Connect it
 * after PassSystem so the UI lands on top of the picture, and before FrameEnd.
 * The image is not cleared, the UI is blended over what the passes wrote.
 */
class End : public moss::System {
public:
    void tick(const Key<key::READ>& key) override;
};

}
