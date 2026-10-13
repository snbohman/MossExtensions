#include "moss/commands/primitives.hpp"
#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/core/frame.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include <vulkan/vulkan_core.h>

namespace moss::extensions::vulkan {

void FrameBegin::tick(const Key<key::READ>& key) {
    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    VkDevice device = core.foundation.device;

    // Poll first, then check: otherwise close detection lags one frame
    glfwPollEvents();
    if (glfwWindowShouldClose(core.foundation.window)) {
        cmd::Quit::init(key).quit();
    }

    RenderCore::FrameData& frame = core.currentFrame();

    // Wait until the gpu has finished rendering the last frame.
    // Timeout in nanoseconds :O
    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, true, 1000000000));

    // Flush objects in per frame dqueue
    frame.dqueue.flush();

    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));

    // Requesting image from the swapchain. SUBOPTIMAL is a success code here
    VK_CHECK_SWAPCHAIN(vkAcquireNextImageKHR(
        device,
        core.swapchain.swapchain,
        1000000000,
        frame.swapchainSemaphore,
        nullptr,
        &core.frame.swapchainImageIndex
    ));

    // Resetting the frame's command buffer after waiting for semaphores and fences
    VkCommandBuffer cmdBuf = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmdBuf, 0));
    core.frame.cmd = cmdBuf;

    // Mapping the drawing dimensions to the drawImageExtent.
    core.drawImage.drawExtent.width  = core.drawImage.imageExtent.width;
    core.drawImage.drawExtent.height = core.drawImage.imageExtent.height;

    VkCommandBufferBeginInfo cmdBeginInfo = utils::info::cmdBufBegin(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    );

    // Begin Command Sequence To Buffer (simply a BeginDraw())
    VK_CHECK(vkBeginCommandBuffer(cmdBuf, &cmdBeginInfo));

    // Transition our main draw image into general layout so attachments can write into it
    utils::transitionImage(
        cmdBuf, core.drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL
    );
}

void FrameEnd::tick(const Key<key::READ>& key) {
    RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
    VkCommandBuffer cmdBuf = core.frame.cmd;
    VkImage swapchainImage = core.swapchain.images[core.frame.swapchainImageIndex];

    // Transition the draw image and the swapchain image into their correct transfer layouts
    utils::transitionImage(
        cmdBuf, core.drawImage.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    utils::transitionImage(
        cmdBuf, swapchainImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    // Execute a copy from the draw image into the swapchain
    utils::copyImage(
        cmdBuf, core.drawImage.image, swapchainImage,
        core.drawImage.drawExtent, core.swapchain.extent
    );

    // Set swapchain image layout to present so we can show it on the screen
    utils::transitionImage(
        cmdBuf, swapchainImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

    // Finalize the command buffer
    VK_CHECK(vkEndCommandBuffer(cmdBuf));

    RenderCore::FrameData& frame = core.currentFrame();
    VkSemaphore renderSemaphore = core.renderSemaphores[core.frame.swapchainImageIndex];

    // Prepare the submission to the queue.
    // we wait on the swapchain semaphore (signaled when the image is ready)
    // and signal the render semaphore (rendering has finished).
    // The swapchain image is first written by a transfer (the blit) and the
    // work is compute and transfer, so COLOR_ATTACHMENT_OUTPUT / ALL_GRAPHICS
    // would not cover it: use ALL_COMMANDS on both sides.
    VkCommandBufferSubmitInfo cmdinfo = utils::info::cmdBufSubmit(cmdBuf);

    VkSemaphoreSubmitInfo waitInfo = utils::info::semaphoreSubmit(
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        frame.swapchainSemaphore
    );

    VkSemaphoreSubmitInfo signalInfo = utils::info::semaphoreSubmit(
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        renderSemaphore
    );

    VkSubmitInfo2 submit = utils::info::submit(&cmdinfo, &signalInfo, &waitInfo);

    // Submit command buffer to the queue and execute it.
    // renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(core.foundation.graphicsQueue, 1, &submit, frame.renderFence));

    // Present: wait on the render semaphore so drawing has finished
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &core.swapchain.swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &core.frame.swapchainImageIndex;

    VK_CHECK_SWAPCHAIN(vkQueuePresentKHR(core.foundation.graphicsQueue, &presentInfo));

    // Tick the frame number as a final last step
    core.frame.frameNumber++;
}

}
