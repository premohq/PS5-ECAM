// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: frame generation on the Wii U side (#66). MihawkGen (Mihawk-99's PS5_MihawkGen,
// .deps/PS5_MihawkGen) makes the picture between the frame Cemu presented last and this one from
// their colour alone, and the TV shows it before this one: a game at 30 frames a second is shown at
// 60 (at 60, on a TV at 120 Hz). Cemu's VulkanRenderer::SwapBuffer calls it (patches/cemu) with the
// swapchain image it is about to present.
//
// It runs only while the game takes two refreshes or more a frame, as its pace over the last frames
// shows, and the setting is on (Settings > Graphics, or a game's own: PS5Cemu_FrameGeneration); a
// faster game is shown as it is. While it runs the pictures come two a frame, so the game cannot show
// that it has got faster: every 600 frames it is left alone for 16, and stays so when it then takes
// under 1.5 refreshes a frame. Each real frame is copied into one of two history images; the frame
// between is written to an image of its own and blitted into the swapchain image, which is presented,
// and the real frame then goes into the next swapchain image. While it runs, VideoOut holds each of
// the two pictures for half the frame: a refresh, or two for a game at 30 fps on a TV at 120 Hz (frame
// pacing's even frame rate would otherwise hold each for the whole frame's refreshes:
// PS5Cemu_FrameGenActive). Its network is the app's eden/mihawkgen/network.fgw, the one Eden's own
// frame generation reads; without it the two frames are blended.
//
// The frames it makes the one between from are the TV's pictures alone (#94): CaptureClean copies
// each as it is drawn, before the second screen, the in-game menu, the keyboard and Cemu's
// notifications go over it. Wherever those change a pixel of the frame, the frame between shows the
// frame's own pixel (shaders/overlays.comp), so they are never made between two frames.

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#include <cstdint>

namespace ps5framegen
{
	struct Swap
	{
		VkDevice device;
		VkPhysicalDevice physical;
		VkCommandBuffer commands; // the renderer's current one, outside a render pass
		VkImage image;			  // the picture about to be presented
		VkFormat format;
		VkExtent2D extent;
		VkImageLayout layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // image's, before and after
		// whether the device may be waited idle, to make the images again at a new size or format
		// (not on the 3DS: Azahar's present thread uses the queue meanwhile)
		bool canWaitIdle = true;
	};

	// Whether CaptureClean has anything to do this frame (frames are being made between)
	bool CleanWanted();
	// Once a frame, when the TV's picture is in swap.image and before the second screen and the overlays
	// go over it (Cemu's PS5_PostEffects): records its copy, the frame the one between is made from
	void CaptureClean(const Swap& swap);
	// Once a real frame, before it is presented: whether the frame between is to be made
	bool Wanted();
	// Records this frame's copy into the history and, when the frame before it is there, the frame
	// between them into swap.image, left in its layout. True: swap.image holds the frame between;
	// present it, acquire the next image and call RecordReal on it. False: swap.image holds this frame.
	bool RecordGenerated(const Swap& swap);
	// Records this frame as shown (kept by RecordGenerated) into image, left in PRESENT_SRC_KHR
	void RecordReal(VkCommandBuffer commands, VkImage image);
}

// The 3DS side's (patches/azahar, its RendererVulkan::RenderToWindow): at each picture Azahar draws for
// the TV into image (TRANSFER_SRC_OPTIMAL, left so), on its renderer's command buffer, outside a render
// pass. unique: a new frame of the game, else the last one drawn again (the 3DS shows a picture at every
// refresh, a game at 30 fps each frame twice). On a new frame of a game at two refreshes a frame or
// more, image becomes the picture between the frame before and this one, and the repeat after it shows
// this one: one refresh later, as many pictures as before. Cemu's Vulkan entry points are loaded for
// Azahar's device first (one emulator runs a session).
void AzaharPS5_FrameGen(VkInstance instance, VkDevice device, VkPhysicalDevice physical, VkCommandBuffer commands, VkImage image,
	VkFormat format, uint32_t width, uint32_t height, bool unique);
