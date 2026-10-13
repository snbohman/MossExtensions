#pragma once

#include "moss/extensions/vulkan/meta.hpp"

namespace moss::extensions::vulkan {

/*
 * Creates the SimImage images and the shared sampler, publishes them as the
 * GpuResources resource, and zero-clears every image once on the first frame.
 *
 * Connect it after FrameBegin and before PassSystem (and before any attachment
 * that binds the images):
 *
 *     mirror.connect<
 *         vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem, vk::FrameEnd
 *     >();
 *
 * Needs no entity of its own: it reads SimImage (with optional ImageScale and
 * DoubleBuffered) and SamplerSettings from the entities of its Context.
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
