#pragma once

#include "moss/extensions/vulkan/meta.hpp"

namespace moss::extensions::vulkan {

/*
 * Creates the SimImage images, the SimBuffer buffers and the shared sampler,
 * publishes them as the GpuResources resource, and zero-clears every image
 * (and zero-fills every buffer) once on the first frame.
 *
 * Connect it after FrameBegin and before PassSystem (and before any attachment
 * that binds the images or buffers):
 *
 *     mirror.connect<
 *         vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem, vk::FrameEnd
 *     >();
 *
 * Needs no entity of its own: it reads SimImage (with optional ImageScale,
 * ImageExtent and DoubleBuffered), SimBuffer (with optional DoubleBuffered) and
 * SamplerSettings from the entities of its Context.
 *
 * Everything it creates is fixed in size and does not follow the window, so it
 * needs no refresh when the swapchain is recreated.
 *
 * It has no Vulkan teardown. Every destroy lambda goes to core.dqueue, which
 * Core::exit flushes. Its exit only erases the plain-data resource and never
 * touches RenderCore, so it is safe before or after Core::exit.
 */
class ImageSystem : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;
    void exit(const Key<key::WRITE>& key) override;

private:
    bool m_ready = false;   // one-time transition + clear done?
};

}
