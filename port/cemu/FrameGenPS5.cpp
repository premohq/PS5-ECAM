// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrameGenPS5.h"
#include "framegen.h"
#include "framegen_shaders.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// the port's (app/emulator.cpp): the setting, the TV's refresh, VideoOut's flips while frames are
// generated (each picture held for refreshes), the network's file and the boot log
uint32_t PS5Cemu_FrameGeneration();
uint32_t PS5Cemu_RefreshMilliHz();
void PS5Cemu_FrameGenActive(bool active, uint32_t refreshes);
std::string PS5Cemu_FrameGenNetwork();
void PS5Cemu_LogLine(std::string_view line);

namespace ps5framegen
{
	namespace
	{
		constexpr uint32_t kSlots = 3;		   // descriptor sets: the frames the GPU may still be on, and one
		constexpr uint32_t kProbeEvery = 600;  // frames generated before the game's own pace is looked at again
		constexpr uint32_t kProbeFrames = 16;  // the frames it is looked at over
		constexpr double kOn = 1.75, kOff = 1.5; // refreshes a frame: on from, off under
		constexpr double kSlowest = 4.5;		 // refreshes a frame past which nothing is made
		constexpr double kGapMs = 250;			 // a frame this long after the one before (a load, a pause): none made between
		constexpr uint32_t kGapPictures = 15;	 // the same on the 3DS, in pictures since its last new frame

		struct Image
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		};

		struct State
		{
			VkDevice device = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkExtent2D extent{};
			fg_context* context = nullptr;
			bool network = false; // the neural method's weights loaded; else the two frames blended
			bool failed = false;  // could not be made: no frames generated in this session
			Image history[2];	  // the last two real frames
			Image generated;	  // the frame between, written by the compute passes
			Image encoded;		  // an sRGB swapchain's: the frame between as sRGB values, blitted into it as they are
			Image shown;		  // this frame as shown, overlays and all, while the history holds its TV picture alone
			int next = 0;		  // the history image the next real frame goes into
			int real = 0;		  // the one this frame went into (RecordReal's)
			bool previous = false; // the other history image holds the frame before
			bool clean[2]{};	   // whether each history image holds a TV picture alone (CaptureClean's)
			bool realShown = false; // RecordReal's picture is shown, not the history's
			// the overlays' pass over the frame between (shaders/overlays.comp, #94): a descriptor set for each
			// history image as the frame's own
			VkDescriptorSetLayout overlaysSetLayout = VK_NULL_HANDLE;
			VkPipelineLayout overlaysLayout = VK_NULL_HANDLE;
			VkPipeline overlays = VK_NULL_HANDLE;
			VkDescriptorPool overlaysPool = VK_NULL_HANDLE;
			VkDescriptorSet overlaysSets[2]{};
			// CaptureClean's this frame: the swapchain image it copied, and Wanted's for RecordGenerated
			VkImage captured = VK_NULL_HANDLE;
			VkImage cleanFrame = VK_NULL_HANDLE;
			uint32_t slot = 0;
			// the game's pace
			std::chrono::steady_clock::time_point last{};
			double averageMs = 0;
			bool active = false;
			uint32_t frames = 0; // generated since the last look at the game's own pace
			uint32_t probe = 0;	 // frames left in the look
			double probeMs = 0;
			uint32_t probeCount = 0;
			uint64_t made = 0;
			// the 3DS's pace (AzaharPS5_FrameGen): pictures drawn since the game's last new frame
			uint32_t repeats = 0;
			double averageRefreshes = 0;
		} s;

		void Log(const std::string& line)
		{
			PS5Cemu_LogLine(line);
		}

		double RefreshPeriodMs()
		{
			const uint32_t refresh = PS5Cemu_RefreshMilliHz();
			return refresh ? 1e6 / refresh : 1000.0 / 59.94;
		}

		// The format that reads an sRGB image's bits as they are. The history is kept in it, so the network
		// sees the sRGB values its pictures were learnt from, and the frame between keeps 8 bits of them
		// (stored linear, the dark parts would band).
		VkFormat Unorm(VkFormat format)
		{
			switch (format)
			{
			case VK_FORMAT_B8G8R8A8_SRGB:
				return VK_FORMAT_B8G8R8A8_UNORM;
			case VK_FORMAT_R8G8B8A8_SRGB:
				return VK_FORMAT_R8G8B8A8_UNORM;
			case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
				return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
			default:
				return format;
			}
		}

		void FreeImage(Image& image)
		{
			if (image.view)
				vkDestroyImageView(s.device, image.view, nullptr);
			if (image.image)
				vkDestroyImage(s.device, image.image, nullptr);
			if (image.memory)
				vkFreeMemory(s.device, image.memory, nullptr);
			image = {};
		}

		bool MakeImage(VkPhysicalDevice physical, VkFormat format, VkImageUsageFlags usage, Image& out, std::string& why, bool withView = true)
		{
			VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = format;
			info.extent = {s.extent.width, s.extent.height, 1};
			info.mipLevels = 1;
			info.arrayLayers = 1;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = usage;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkResult result = vkCreateImage(s.device, &info, nullptr, &out.image);
			if (result != VK_SUCCESS)
				return why = fmt::format("vkCreateImage: {}", (int)result), false;
			VkMemoryRequirements needs;
			vkGetImageMemoryRequirements(s.device, out.image, &needs);
			VkPhysicalDeviceMemoryProperties memory;
			vkGetPhysicalDeviceMemoryProperties(physical, &memory);
			uint32_t type = UINT32_MAX;
			for (uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; i++)
				if ((needs.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
					type = i;
			for (uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; i++)
				if (needs.memoryTypeBits & (1u << i))
					type = i;
			VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			allocate.allocationSize = needs.size;
			allocate.memoryTypeIndex = type;
			if (type == UINT32_MAX || (result = vkAllocateMemory(s.device, &allocate, nullptr, &out.memory)) != VK_SUCCESS)
				return why = fmt::format("vkAllocateMemory: {}", (int)result), false;
			if ((result = vkBindImageMemory(s.device, out.image, out.memory, 0)) != VK_SUCCESS)
				return why = fmt::format("vkBindImageMemory: {}", (int)result), false;
			if (!withView)
				return true;
			VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			view.image = out.image;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = format;
			view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			if ((result = vkCreateImageView(s.device, &view, nullptr, &out.view)) != VK_SUCCESS)
				return why = fmt::format("vkCreateImageView: {}", (int)result), false;
			return true;
		}

		void FreeOverlays()
		{
			FreeImage(s.shown);
			if (s.overlays)
				vkDestroyPipeline(s.device, s.overlays, nullptr);
			if (s.overlaysLayout)
				vkDestroyPipelineLayout(s.device, s.overlaysLayout, nullptr);
			if (s.overlaysPool)
				vkDestroyDescriptorPool(s.device, s.overlaysPool, nullptr); // and its sets
			if (s.overlaysSetLayout)
				vkDestroyDescriptorSetLayout(s.device, s.overlaysSetLayout, nullptr);
			s.overlays = VK_NULL_HANDLE;
			s.overlaysLayout = VK_NULL_HANDLE;
			s.overlaysPool = VK_NULL_HANDLE;
			s.overlaysSetLayout = VK_NULL_HANDLE;
			s.captured = s.cleanFrame = VK_NULL_HANDLE;
		}

		void Release()
		{
			if (s.context)
				fg_context_destroy(s.context);
			s.context = nullptr;
			for (Image& image : s.history)
				FreeImage(image);
			FreeImage(s.generated);
			FreeImage(s.encoded);
			FreeOverlays();
			s.network = false;
			s.previous = false;
		}

		// The overlays' pass (#94): its pipeline, and a descriptor set for each history image as the frame's
		// TV picture, with this frame as shown and the frame between. Without it, the frame between is made
		// from the frames as shown, overlays and all, as before.
		bool MakeOverlays(VkPhysicalDevice physical, std::string& why)
		{
			if (!MakeImage(physical, Unorm(s.format), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					s.shown, why))
				return false;
			VkDescriptorSetLayoutBinding bindings[3]{};
			for (uint32_t i = 0; i < 3; i++)
			{
				bindings[i].binding = i;
				bindings[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			}
			VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			setLayout.bindingCount = 3;
			setLayout.pBindings = bindings;
			VkResult result = vkCreateDescriptorSetLayout(s.device, &setLayout, nullptr, &s.overlaysSetLayout);
			if (result != VK_SUCCESS)
				return why = fmt::format("vkCreateDescriptorSetLayout: {}", (int)result), false;
			VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			layout.setLayoutCount = 1;
			layout.pSetLayouts = &s.overlaysSetLayout;
			if ((result = vkCreatePipelineLayout(s.device, &layout, nullptr, &s.overlaysLayout)) != VK_SUCCESS)
				return why = fmt::format("vkCreatePipelineLayout: {}", (int)result), false;
			VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			module.codeSize = sizeof(shaders::k_overlays_comp);
			module.pCode = shaders::k_overlays_comp;
			VkShaderModule shader = VK_NULL_HANDLE;
			if ((result = vkCreateShaderModule(s.device, &module, nullptr, &shader)) != VK_SUCCESS)
				return why = fmt::format("vkCreateShaderModule: {}", (int)result), false;
			VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
			pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			pipeline.stage.module = shader;
			pipeline.stage.pName = "main";
			pipeline.layout = s.overlaysLayout;
			result = vkCreateComputePipelines(s.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &s.overlays);
			vkDestroyShaderModule(s.device, shader, nullptr);
			if (result != VK_SUCCESS)
			{
				s.overlays = VK_NULL_HANDLE;
				return why = fmt::format("vkCreateComputePipelines: {}", (int)result), false;
			}
			const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
			VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			pool.maxSets = 2;
			pool.poolSizeCount = 2;
			pool.pPoolSizes = sizes;
			if ((result = vkCreateDescriptorPool(s.device, &pool, nullptr, &s.overlaysPool)) != VK_SUCCESS)
				return why = fmt::format("vkCreateDescriptorPool: {}", (int)result), false;
			const VkDescriptorSetLayout layouts[2] = {s.overlaysSetLayout, s.overlaysSetLayout};
			VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			allocate.descriptorPool = s.overlaysPool;
			allocate.descriptorSetCount = 2;
			allocate.pSetLayouts = layouts;
			if ((result = vkAllocateDescriptorSets(s.device, &allocate, s.overlaysSets)) != VK_SUCCESS)
				return why = fmt::format("vkAllocateDescriptorSets: {}", (int)result), false;
			for (int i = 0; i < 2; i++)
			{
				const VkDescriptorImageInfo images[3] = {{VK_NULL_HANDLE, s.shown.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
					{VK_NULL_HANDLE, s.history[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {VK_NULL_HANDLE, s.generated.view, VK_IMAGE_LAYOUT_GENERAL}};
				VkWriteDescriptorSet writes[3]{};
				for (uint32_t b = 0; b < 3; b++)
				{
					writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writes[b].dstSet = s.overlaysSets[i];
					writes[b].dstBinding = b;
					writes[b].descriptorCount = 1;
					writes[b].descriptorType = bindings[b].descriptorType;
					writes[b].pImageInfo = &images[b];
				}
				vkUpdateDescriptorSets(s.device, 3, writes, 0, nullptr);
			}
			return true;
		}

		// The generator and its images, for this swapchain's format and size (made again when they change)
		bool Prepare(const Swap& swap)
		{
			if (s.failed)
				return false;
			if (s.context && s.device == swap.device && s.format == swap.format && s.extent.width == swap.extent.width &&
				s.extent.height == swap.extent.height)
				return true;
			if (s.context)
			{
				if (!swap.canWaitIdle)
				{
					// the GPU may still be on the images: they stay, unused, till the game ends
					Log(fmt::format("[framegen] the pictures changed to {}x{}: no more frames generated in this session", swap.extent.width,
						swap.extent.height));
					s.failed = true;
					return false;
				}
				vkDeviceWaitIdle(s.device);
				Release();
			}
			s.device = swap.device;
			s.format = swap.format;
			s.extent = swap.extent;
			fg_create_info info{};
			info.device = swap.device;
			info.physical_device = swap.physical;
			info.width = swap.extent.width;
			info.height = swap.extent.height;
			info.slots = kSlots;
			info.output_format = VK_FORMAT_R8G8B8A8_UNORM;
			char error[256] = {};
			std::string why;
			if (fg_context_create(&info, &s.context, error, sizeof(error)) != VK_SUCCESS)
				why = std::string("the generator: ") + error;
			else if (!MakeImage(swap.physical, Unorm(swap.format), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						 s.history[0], why) ||
				!MakeImage(swap.physical, Unorm(swap.format), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					s.history[1], why) ||
				!MakeImage(swap.physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, s.generated, why) ||
				(Unorm(swap.format) != swap.format &&
					!MakeImage(swap.physical, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, s.encoded, why, false)))
				why = "its images: " + why;
			if (!why.empty())
			{
				Log(fmt::format("[framegen] not made ({}): no frames generated in this session", why));
				Release();
				s.failed = true;
				return false;
			}
			// the Wii U side's only, where Cemu calls CaptureClean (the 3DS's swap is the one that cannot wait idle)
			if (swap.canWaitIdle && !MakeOverlays(swap.physical, why))
			{
				Log(fmt::format("[framegen] the overlays' pass not made ({}): the frame between is made with the overlays in it", why));
				FreeOverlays();
			}
			// the network, as Eden's frame generation reads it
			const std::string path = PS5Cemu_FrameGenNetwork();
			std::vector<char> weights;
			if (std::FILE* file = std::fopen(path.c_str(), "rb"))
			{
				char buffer[65536];
				for (size_t got; (got = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
					weights.insert(weights.end(), buffer, buffer + got);
				std::fclose(file);
			}
			if (weights.empty())
				Log(fmt::format("[framegen] no network at {}: the two frames are blended", path));
			else if (fg_context_load_network(s.context, weights.data(), weights.size(), error, sizeof(error)) != VK_SUCCESS)
				Log(fmt::format("[framegen] the network at {} did not load ({}): the two frames are blended", path, error));
			else
				s.network = true;
			Log(fmt::format("[framegen] ready for {}x{} pictures{}: {}{}", swap.extent.width, swap.extent.height, s.encoded.image ? " (sRGB)" : "",
				s.network ? "MihawkGen's network" : "a blend", s.overlays ? ", the overlays left out" : ""));
			return true;
		}

		void Barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
			VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
		{
			VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout = from;
			barrier.newLayout = to;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		// One of the generator's images into a layout, from whatever it was last used for
		void Move(VkCommandBuffer commands, Image& image, VkImageLayout to, VkAccessFlags dstAccess, VkPipelineStageFlags dstStage)
		{
			Barrier(commands, image.image, image.layout, to, VK_ACCESS_MEMORY_WRITE_BIT, dstAccess, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dstStage);
			image.layout = to;
		}

		void Copy(VkCommandBuffer commands, VkImage from, VkImage to)
		{
			VkImageCopy region{};
			region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			region.extent = {s.extent.width, s.extent.height, 1};
			vkCmdCopyImage(commands, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}

		// How long VideoOut holds each of a frame's two pictures: half the frame, in whole refreshes. Two for
		// a game at 30 fps on a TV at 120 Hz (else the frame between would show for one refresh and the
		// frame itself for three), one otherwise.
		uint32_t PictureRefreshes(double refreshesAFrame)
		{
			return refreshesAFrame >= 3.5 ? 2 : 1;
		}

		void Activate(bool active, const char* why, double fps, uint32_t refreshes = 1)
		{
			s.active = active;
			s.previous = false; // the frame before is not the one before any more
			s.frames = 0;
			PS5Cemu_FrameGenActive(active, refreshes);
			Log(fmt::format("[framegen] {} ({}): the game at {:.1f} fps, the TV at {:.2f} Hz{}; {} frames made so far", active ? "on" : "off", why,
				fps, 1000.0 / RefreshPeriodMs(), active && refreshes > 1 ? fmt::format(", each picture shown for {} refreshes", refreshes) : "",
				s.made));
		}

		double Fps()
		{
			return s.averageMs > 0 ? 1000.0 / s.averageMs : 0.0;
		}
	}

	bool CleanWanted()
	{
		return PS5Cemu_FrameGeneration() != 0 && !s.failed && s.active && s.probe == 0 && (!s.context || s.overlays);
	}

	void CaptureClean(const Swap& swap)
	{
		s.captured = VK_NULL_HANDLE;
		if (!CleanWanted() || !Prepare(swap) || !s.overlays)
			return;
		VkCommandBuffer commands = swap.commands;
		Image& clean = s.history[s.next];
		// (after the TV's picture is drawn, and before the second screen and the overlays draw over it)
		Barrier(commands, swap.image, swap.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		Move(commands, clean, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		Copy(commands, swap.image, clean.image);
		Move(commands, clean, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
		Barrier(commands, swap.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap.layout, VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		s.captured = swap.image;
	}

	bool Wanted()
	{
		// this frame's TV picture alone, when CaptureClean copied it (each frame's own, never an earlier one's)
		s.cleanFrame = s.captured;
		s.captured = VK_NULL_HANDLE;
		const auto now = std::chrono::steady_clock::now();
		const double ms = s.last.time_since_epoch().count() ? std::chrono::duration<double, std::milli>(now - s.last).count() : 0.0;
		s.last = now;
		const double period = RefreshPeriodMs();
		if (PS5Cemu_FrameGeneration() == 0 || s.failed)
		{
			if (s.active || s.probe)
			{
				s.probe = 0;
				Activate(false, "turned off", Fps());
			}
			return false;
		}
		// after a load or a pause, the frame before is too old to make one between
		if (ms >= kGapMs)
			s.previous = false;
		// a look at the game's own pace: no frames made, VideoOut as the setting has it
		if (s.probe > 0)
		{
			if (ms > 0 && ms < kGapMs)
			{
				s.probeMs += ms;
				s.probeCount++;
			}
			if (--s.probe == 0)
			{
				s.averageMs = s.probeCount ? s.probeMs / s.probeCount : s.averageMs;
				if (s.averageMs >= period * kOff)
					PS5Cemu_FrameGenActive(true, PictureRefreshes(s.averageMs / period)), s.previous = false;
				else
					Activate(false, "the game got faster", Fps());
				return s.active && s.averageMs >= period * kOff;
			}
			return false;
		}
		if (!s.active)
		{
			if (ms > 0 && ms < kGapMs)
				s.averageMs = s.averageMs > 0 ? s.averageMs * 0.9 + ms * 0.1 : ms;
			if (s.averageMs >= period * kOn && s.averageMs <= period * kSlowest)
				Activate(true, "the game takes two refreshes a frame or more", Fps(), PictureRefreshes(s.averageMs / period));
			return s.active;
		}
		if (++s.frames % kProbeEvery == 0)
		{
			s.probe = kProbeFrames;
			s.probeMs = 0;
			s.probeCount = 0;
			PS5Cemu_FrameGenActive(false, 1);
			return false;
		}
		return true;
	}

	bool RecordGenerated(const Swap& swap)
	{
		if (!Prepare(swap))
			return false;
		VkCommandBuffer commands = swap.commands;
		const int now = s.next, before = 1 - now;
		// the TV's picture alone is in the history already (CaptureClean): the frame as shown is kept for
		// its own picture, the overlays then go over the frame between from it
		const bool clean = s.overlays && s.cleanFrame == swap.image;
		s.cleanFrame = VK_NULL_HANDLE;
		Image& kept = clean ? s.shown : s.history[now];
		// this frame into the history, or as shown
		// (after everything before it on the queue: the frame's draws)
		Barrier(commands, swap.image, swap.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		Move(commands, kept, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		Copy(commands, swap.image, kept.image);
		Move(commands, kept, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
		// a frame between two frames of the same kind only: never from one with the overlays and one without
		const bool had = s.previous && s.clean[before] == clean;
		s.previous = true;
		s.clean[now] = clean;
		s.real = now;
		s.realShown = clean;
		s.next = before;
		if (!had)
		{
			// the first frame: shown as it is, the next one gets a frame before it
			Barrier(commands, swap.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap.layout, VK_ACCESS_TRANSFER_READ_BIT, 0,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
			return false;
		}
		// the frame between the one before and this one
		Move(commands, s.generated, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		fg_frame_pair pair{};
		pair.prev_color = s.history[before].view;
		pair.next_color = s.history[now].view;
		pair.t = 0.5f;
		fg_record_generate(s.context, commands, s.slot, s.network ? FG_METHOD_NEURAL : FG_METHOD_BLEND, &pair, s.generated.view);
		s.slot = (s.slot + 1) % kSlots;
		if (clean)
		{
			// the second screen and the overlays over it as this frame shows them (shaders/overlays.comp)
			Barrier(commands, s.generated.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, s.overlays);
			vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, s.overlaysLayout, 0, 1, &s.overlaysSets[now], 0, nullptr);
			vkCmdDispatch(commands, (s.extent.width + 7) / 8, (s.extent.height + 7) / 8, 1);
		}
		Barrier(commands, s.generated.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		s.generated.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		// an sRGB swapchain's: the values copied as they are into an sRGB image, which a blit into the
		// swapchain's sRGB format leaves unchanged (from the frame between's own, it would encode them again)
		VkImage source = s.generated.image;
		if (s.encoded.image)
		{
			Move(commands, s.encoded, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			Copy(commands, s.generated.image, s.encoded.image);
			Move(commands, s.encoded, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			source = s.encoded.image;
		}
		// into the swapchain image, in its format
		Barrier(commands, swap.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkImageBlit blit{};
		blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		blit.srcOffsets[1] = {(int32_t)s.extent.width, (int32_t)s.extent.height, 1};
		blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		blit.dstOffsets[1] = blit.srcOffsets[1];
		vkCmdBlitImage(commands, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
			VK_FILTER_NEAREST);
		Barrier(commands, swap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, swap.layout, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		if (s.made++ == 0)
			Log(fmt::format("[framegen] the first frame made ({})", s.network ? "network" : "blend"));
		return true;
	}

	void RecordReal(VkCommandBuffer commands, VkImage image)
	{
		Image& real = s.realShown ? s.shown : s.history[s.real];
		Move(commands, real, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		// (after the wait for the image, in the submission Cemu made as it acquired it)
		Barrier(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		Copy(commands, real.image, image);
		Barrier(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		Move(commands, real, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
	}
}

void AzaharPS5_FrameGen(VkInstance instance, VkDevice device, VkPhysicalDevice physical, VkCommandBuffer commands, VkImage image,
	VkFormat format, uint32_t width, uint32_t height, bool unique)
{
	using namespace ps5framegen;
	if (PS5Cemu_FrameGeneration() == 0 || s.failed)
	{
		if (s.active)
			Activate(false, "turned off", s.averageRefreshes > 0 ? 1000.0 / RefreshPeriodMs() / s.averageRefreshes : 0.0);
		s.repeats = 0;
		return;
	}
	// Cemu's entry points for Azahar's device, which MihawkGen calls through
	static VkDevice s_loaded = VK_NULL_HANDLE;
	if (s_loaded != device)
	{
		if (!InitializeGlobalVulkan() || !InitializeInstanceVulkan(instance) || !InitializeDeviceVulkan(device))
		{
			Log("[framegen] the driver's entry points could not be loaded for the 3DS's renderer: no frames generated in this session");
			s.failed = true;
			return;
		}
		s_loaded = device;
	}
	// the game's pace: pictures between its new frames, two for a game at 30 fps
	s.repeats++;
	if (!unique)
		return;
	const uint32_t refreshes = s.repeats;
	s.repeats = 0;
	if (refreshes > kGapPictures)
		// a pause (the menu, a load): not the game's pace, and the frame before is too old to make one between
		s.previous = false;
	else
	{
		s.averageRefreshes = s.averageRefreshes > 0 ? s.averageRefreshes * 0.9 + refreshes * 0.1 : refreshes;
		const bool wanted = s.averageRefreshes >= (s.active ? kOff : kOn) && s.averageRefreshes <= kSlowest;
		if (wanted != s.active)
		{
			const char* why = wanted ? "the game takes two refreshes a frame or more"
				: s.averageRefreshes > kSlowest ? "the game got slower" : "the game got faster";
			Activate(wanted, why, 1000.0 / RefreshPeriodMs() / s.averageRefreshes);
		}
	}
	if (!s.active)
		return;
	Swap swap{device, physical, commands, image, format, {width, height}, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false};
	RecordGenerated(swap);
}
