// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame.h"
#include "../app/amiibo_list.h"
#include "../app/lang.h"
#include "../app/menu_canvas.h"
#include "../app/side_menu.h"
#include "../app/tga.h"
#include "../ps5/log.h"
#include "../ui/text.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace ps5ingamenx
{
	namespace
	{
		using ps5lang::Percent;
		using ps5lang::Tr;
		using ps5lang::TrC;
		using ps5lang::TrF;
		using ps5lang::TrMark;

		// the game, from the session file (Start)
		std::mutex s_mutex;
		std::string s_title, s_details, s_coverPath;
		std::string s_root;		// the app's data folder (its amiibo/)
		std::string s_game;		// the title ID the launcher keeps the game's settings under
		bool s_session = false; // a session file was there: this side's screens are drawn
		Settings s_settings;	// under s_mutex
		bool s_changed = false;
		bool s_everChanged = false; // for the result file: the launcher keeps what the menu changed
		Room s_room;				// under s_mutex

		// the amiibo dumps in the data folder's amiibo/, listed as the menu opens (the renderer's thread),
		// the one asked for (under s_mutex) and what came of the last one
		std::vector<ps5amiibo::Series> s_amiibo; // its folders a series each (app/amiibo_list.h)
		int s_amiiboSeries = 0, s_amiiboIndex = 0;
		std::string s_amiiboRequest;
		bool s_amiiboAsked = false;
		std::string s_amiiboName;
		std::atomic<int> s_amiiboAnswer{-1};

		std::atomic<int> s_step{(int)Step::None};
		std::atomic<size_t> s_shadersDone{0}, s_shadersOf{0};
		std::atomic<bool> s_loading{false};
		std::atomic<bool> s_drawsLoading{false};

		std::atomic<bool> s_ready{false}; // the renderer's ImGui is up: the menu can open
		std::atomic<bool> s_open{false};
		// the menu just closed: the game's controller stays still until player 1 lets go of every button,
		// so the one that closed it (Cross on Resume, Circle) never reaches the game
		std::atomic<bool> s_release{false};

		void CloseMenu()
		{
			s_release = true;
			s_open = false;
		}
		std::atomic<bool> s_menuFresh{false};
		std::atomic<bool> s_leave{false};
		std::atomic<uint32_t> s_buttons{0};
		std::atomic<uint8_t> s_leftX{128}, s_leftY{128};

		// tr: the Switch side's upscaling filters, as the launcher names them (Eden's order: bilinear, FSR 1, bicubic, nearest)
		constexpr const char* kFilters[] = {TrMark("Bilinear"), TrMark("AMD FSR 1"), TrMark("Bicubic"), TrMark("Nearest neighbour")};
		constexpr int kFilterCount = 4;
		constexpr int kSpeedLimits[] = {100, 150, 200, 300, 0}; // percent; 0 is none
		// tr: anti-aliasing in the Switch's in-game menu: none, or Eden's two methods (FXAA and SMAA, their own names)
		constexpr const char* kAntiAliasing[] = {TrMark("Off"), "FXAA", "SMAA"};
		// FSR 1's sharpening, Eden's slider: each step of it halves the sharpening, shown as the share left
		// of the sharpest (100%, 84%, 71%, 50%, 25%)
		constexpr int kSharpening[] = {0, 25, 50, 100, 200};
		constexpr int kSharpeningCount = 5;
		// tr: the Switch's controller types, as the game sees the DualSense (Automatic: a Pro Controller)
		constexpr const char* kControllers[] = {TrMark("Automatic"), TrMark("Pro Controller"), TrMark("Handheld"), TrMark("Joy-Con pair"),
			TrMark("Left Joy-Con"), TrMark("Right Joy-Con")};
		constexpr int kHandheld = 1; // in Settings::controller
		constexpr int kDeadzoneStep = 2, kDeadzoneMost = 30;

		int SharpeningIndex(int value)
		{
			int best = 0;
			for (int i = 1; i < kSharpeningCount; i++)
				if (std::abs(kSharpening[i] - value) < std::abs(kSharpening[best] - value))
					best = i;
			return best;
		}

		std::string SharpeningName(int value)
		{
			return Percent((int)std::lround(100.0 * std::pow(2.0, -value / 100.0)));
		}

		uint64_t Now()
		{
			return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// -- Vulkan, through Eden's device -----------------------------------------------------------

		struct Vk
		{
			PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
			PFN_vkGetPhysicalDeviceMemoryProperties memoryProperties = nullptr;
			PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
			PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
			PFN_vkCreateImage createImage = nullptr;
			PFN_vkGetImageMemoryRequirements imageRequirements = nullptr;
			PFN_vkAllocateMemory allocateMemory = nullptr;
			PFN_vkBindImageMemory bindImageMemory = nullptr;
			PFN_vkCreateImageView createImageView = nullptr;
			PFN_vkCreateSampler createSampler = nullptr;
			PFN_vkCreateBuffer createBuffer = nullptr;
			PFN_vkGetBufferMemoryRequirements bufferRequirements = nullptr;
			PFN_vkBindBufferMemory bindBufferMemory = nullptr;
			PFN_vkMapMemory mapMemory = nullptr;
			PFN_vkUnmapMemory unmapMemory = nullptr;
			PFN_vkCmdPipelineBarrier pipelineBarrier = nullptr;
			PFN_vkCmdCopyBufferToImage copyBufferToImage = nullptr;
		};

		// A picture for ImGui: its image, and the staging buffer it came through (kept: a few, small)
		struct Texture
		{
			ImTextureID id = nullptr;
			int width = 0, height = 0;
		};

		// The rest belongs to Eden's renderer thread, which records the present pass
		struct Gpu
		{
			bool ready = false, failed = false;
			Target target{};
			Vk vk;
			VkPhysicalDeviceMemoryProperties memory{};
			VkDescriptorPool pool = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			ImGuiContext* context = nullptr;
			ImFontAtlas* atlas = nullptr;
			ImFont *head = nullptr, *body = nullptr, *row = nullptr, *small = nullptr, *chip = nullptr, *big = nullptr;
			bool fontsUploaded = false;
			int uploadAge = -1;
			bool pending = false; // a frame's draw data waits for the render pass
			bool picturesTried = false;
			Texture cover, blur;
		};
		Gpu g;
		ps5menu::SideMenu s_side;
		uint32_t s_menuButtons = 0;

		template<typename F>
		bool Load(F& to, const char* name, bool device)
		{
			to = device ? (F)g.vk.getDeviceProcAddr(g.target.device, name) : (F)g.target.getInstanceProcAddr(g.target.instance, name);
			if (!to)
				ps5log::Line("[ingamenx] no {} on Eden's device", name);
			return to != nullptr;
		}

		bool LoadVulkan()
		{
			Vk& v = g.vk;
			v.getDeviceProcAddr = (PFN_vkGetDeviceProcAddr)g.target.getInstanceProcAddr(g.target.instance, "vkGetDeviceProcAddr");
			if (!v.getDeviceProcAddr)
				return false;
			bool ok = Load(v.memoryProperties, "vkGetPhysicalDeviceMemoryProperties", false);
			ok &= Load(v.getDeviceQueue, "vkGetDeviceQueue", true);
			ok &= Load(v.createDescriptorPool, "vkCreateDescriptorPool", true);
			ok &= Load(v.createImage, "vkCreateImage", true);
			ok &= Load(v.imageRequirements, "vkGetImageMemoryRequirements", true);
			ok &= Load(v.allocateMemory, "vkAllocateMemory", true);
			ok &= Load(v.bindImageMemory, "vkBindImageMemory", true);
			ok &= Load(v.createImageView, "vkCreateImageView", true);
			ok &= Load(v.createSampler, "vkCreateSampler", true);
			ok &= Load(v.createBuffer, "vkCreateBuffer", true);
			ok &= Load(v.bufferRequirements, "vkGetBufferMemoryRequirements", true);
			ok &= Load(v.bindBufferMemory, "vkBindBufferMemory", true);
			ok &= Load(v.mapMemory, "vkMapMemory", true);
			ok &= Load(v.unmapMemory, "vkUnmapMemory", true);
			ok &= Load(v.pipelineBarrier, "vkCmdPipelineBarrier", true);
			ok &= Load(v.copyBufferToImage, "vkCmdCopyBufferToImage", true);
			if (ok)
				v.memoryProperties(g.target.physicalDevice, &g.memory);
			return ok;
		}

		int MemoryType(uint32_t bits, VkMemoryPropertyFlags wanted)
		{
			for (uint32_t i = 0; i < g.memory.memoryTypeCount; i++)
				if ((bits & (1u << i)) && (g.memory.memoryTypes[i].propertyFlags & wanted) == wanted)
					return (int)i;
			return -1;
		}

		bool Allocate(const VkMemoryRequirements& needs, VkMemoryPropertyFlags wanted, VkDeviceMemory& memory)
		{
			int type = MemoryType(needs.memoryTypeBits, wanted);
			if (type < 0 && (wanted & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
				type = MemoryType(needs.memoryTypeBits, 0);
			if (type < 0)
				return false;
			VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			info.allocationSize = needs.size;
			info.memoryTypeIndex = (uint32_t)type;
			return g.vk.allocateMemory(g.target.device, &info, nullptr, &memory) == VK_SUCCESS;
		}

		// An RGBA picture (top row first) on the GPU: its copy recorded into the frame's command buffer,
		// outside the render pass, and a descriptor for ImGui
		Texture MakeTexture(VkCommandBuffer cmd, const std::vector<uint8_t>& rgba, int width, int height)
		{
			Texture texture;
			const VkDevice device = g.target.device;
			const Vk& v = g.vk;
			VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			image.imageType = VK_IMAGE_TYPE_2D;
			image.format = VK_FORMAT_R8G8B8A8_UNORM;
			image.extent = {(uint32_t)width, (uint32_t)height, 1};
			image.mipLevels = 1;
			image.arrayLayers = 1;
			image.samples = VK_SAMPLE_COUNT_1_BIT;
			image.tiling = VK_IMAGE_TILING_OPTIMAL;
			image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImage handle = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkMemoryRequirements needs{};
			if (v.createImage(device, &image, nullptr, &handle) != VK_SUCCESS)
				return texture;
			v.imageRequirements(device, handle, &needs);
			if (!Allocate(needs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory) || v.bindImageMemory(device, handle, memory, 0) != VK_SUCCESS)
				return texture;
			VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			view.image = handle;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = VK_FORMAT_R8G8B8A8_UNORM;
			view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			VkImageView viewHandle = VK_NULL_HANDLE;
			if (v.createImageView(device, &view, nullptr, &viewHandle) != VK_SUCCESS)
				return texture;
			// the pixels, through a buffer the CPU writes
			VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			buffer.size = rgba.size();
			buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VkBuffer staging = VK_NULL_HANDLE;
			VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
			if (v.createBuffer(device, &buffer, nullptr, &staging) != VK_SUCCESS)
				return texture;
			v.bufferRequirements(device, staging, &needs);
			void* mapped = nullptr;
			if (!Allocate(needs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingMemory) ||
				v.bindBufferMemory(device, staging, stagingMemory, 0) != VK_SUCCESS || v.mapMemory(device, stagingMemory, 0, rgba.size(), 0, &mapped) != VK_SUCCESS)
				return texture;
			std::memcpy(mapped, rgba.data(), rgba.size());
			v.unmapMemory(device, stagingMemory);
			VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = handle;
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			v.pipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			VkBufferImageCopy region{};
			region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
			v.copyBufferToImage(cmd, staging, handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			v.pipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			texture.id = (ImTextureID)ImGui_ImplVulkan_AddTexture(g.sampler, viewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			texture.width = width;
			texture.height = height;
			return texture;
		}

		// The cover, and a small copy of it (each pixel the average of those it covers) that drawn large is
		// its colours blurred, for the loading screen's ground
		void MakePictures(VkCommandBuffer cmd)
		{
			g.picturesTried = true;
			std::string path;
			{
				std::lock_guard lock(s_mutex);
				path = s_coverPath;
			}
			std::vector<uint8_t> rgba;
			int width = 0, height = 0;
			if (path.empty() || !ps5tga::Load(path, 512, 704, rgba, width, height))
			{
				ps5log::Line("[ingamenx] no cover for the menu ({})", path.empty() ? "none given" : path);
				return;
			}
			g.cover = MakeTexture(cmd, rgba, width, height);
			std::vector<uint8_t> small;
			int sw = 0, sh = 0;
			if (ps5tga::Load(path, 24, 24, small, sw, sh))
				g.blur = MakeTexture(cmd, small, sw, sh);
		}

		bool Initialize(const Target& target, float scale)
		{
			g.target = target;
			if (!target.getInstanceProcAddr || !target.instance || !LoadVulkan())
			{
				ps5log::Line("[ingamenx] Eden's Vulkan device gave no functions: Eden's own screens instead");
				return false;
			}
			VkQueue queue = VK_NULL_HANDLE;
			g.vk.getDeviceQueue(target.device, target.queueFamily, 0, &queue);
			const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
			VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.maxSets = 16; // the font, the cover and its blur
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &size;
			VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.maxLod = 1.0f;
			if (!queue || g.vk.createDescriptorPool(target.device, &poolInfo, nullptr, &g.pool) != VK_SUCCESS ||
				g.vk.createSampler(target.device, &sampler, nullptr, &g.sampler) != VK_SUCCESS)
			{
				ps5log::Line("[ingamenx] no descriptor pool or sampler: Eden's own screens instead");
				return false;
			}
			if (!ImGui_ImplVulkan_LoadFunctions([](const char* name, void* user) -> PFN_vkVoidFunction {
					const Gpu& gpu = *(const Gpu*)user;
					PFN_vkVoidFunction function = gpu.vk.getDeviceProcAddr(gpu.target.device, name);
					return function ? function : gpu.target.getInstanceProcAddr(gpu.target.instance, name);
				}, &g))
			{
				ps5log::Line("[ingamenx] ImGui's Vulkan functions did not load: Eden's own screens instead");
				return false;
			}

			// Lexend's weights at the menu's sizes, with the console's font for what Lexend lacks (a Russian
			// menu, a Japanese title), cut to the characters the language's words and the game's name have
			std::string title;
			{
				std::lock_guard lock(s_mutex);
				title = s_title;
			}
			const ImWchar* ranges = ps5menu::GlyphRanges(title);
			int mergeFace = 0;
			const auto& merge = ps5menu::MergeFont(mergeFace);
			g.atlas = new ImFontAtlas();
			bool fonts = true;
			auto add = [&](ps5menu::Weight weight, float size) -> ImFont* {
				ImFontConfig config{};
				config.FontDataOwnedByAtlas = false;
				config.OversampleH = 2;
				const auto& file = ps5menu::FontFile(weight);
				if (file.empty())
				{
					fonts = false;
					return nullptr;
				}
				ImFont* font = g.atlas->AddFontFromMemoryTTF((void*)file.data(), (int)file.size(), size * scale, &config, ranges);
				if (!merge.empty())
				{
					ImFontConfig second{};
					second.FontDataOwnedByAtlas = false;
					second.MergeMode = true;
					second.FontNo = mergeFace;
					second.OversampleH = 2;
					g.atlas->AddFontFromMemoryTTF((void*)merge.data(), (int)merge.size(), size * scale, &second, ranges);
				}
				return font;
			};
			using ps5menu::Weight;
			g.head = add(Weight::SemiBold, ps5menu::kHeading);
			g.body = add(Weight::Medium, ps5menu::kBody);
			g.row = add(Weight::Regular, ps5menu::kLabel);
			g.small = add(Weight::Regular, ps5menu::kCaption);
			g.chip = add(Weight::SemiBold, ps5menu::kChip);
			g.big = add(Weight::SemiBold, 60);
			if (!fonts)
			{
				ps5log::Line("[ingamenx] the menu's fonts are not in {}/ui/fonts: Eden's own screens instead", ps5paths::Assets());
				return false;
			}
			g.context = ImGui::CreateContext(g.atlas);
			ImGui::SetCurrentContext(g.context);
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;

			ImGui_ImplVulkan_InitInfo info{};
			info.Instance = target.instance;
			info.PhysicalDevice = target.physicalDevice;
			info.Device = target.device;
			info.QueueFamily = target.queueFamily;
			info.Queue = queue;
			info.DescriptorPool = g.pool;
			info.MinImageCount = std::max(2u, target.imageCount);
			info.ImageCount = info.MinImageCount;
			if (!ImGui_ImplVulkan_Init(&info, target.renderPass))
			{
				ps5log::Line("[ingamenx] ImGui's Vulkan backend did not start: Eden's own screens instead");
				return false;
			}
			ps5log::Line("[ingamenx] ready: {}x{}, {} images{}", target.width, target.height, info.ImageCount,
				merge.empty() ? std::string() : fmt::format(", second font {}", ps5lang::GetMenuFont().path));
			return true;
		}

		// -- the loading screen ----------------------------------------------------------------------

		std::string StepText()
		{
			switch ((Step)s_step.load())
			{
			case Step::Shaders:
				if (const size_t of = s_shadersOf; of > 0)
					// tr: the Switch side's loading screen: the game's shaders read from the cache, {0} of {1}
					return TrF("Shaders: {0} of {1}", (size_t)s_shadersDone, of);
				return Tr("Loading…");
			case Step::Starting: return Tr("Starting");
			default: return Tr("Loading…");
			}
		}

		void DrawLoading(float scale, float time)
		{
			ImGuiIO& io = ImGui::GetIO();
			const ImVec2 size = io.DisplaySize;
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			const ps5menu::Palette& colours = ps5menu::kRed;
			draw->AddRectFilled({0, 0}, size, ps5menu::Colour(0x05070d));
			// the cover's colours, blurred, over the whole screen; darker on the left, where the words are
			if (g.blur.id)
			{
				const float k = std::max(size.x / g.blur.width, size.y / g.blur.height);
				const ImVec2 half{g.blur.width * k * 0.5f, g.blur.height * k * 0.5f};
				const ImVec2 middle{size.x * 0.5f, size.y * 0.5f};
				draw->AddImage(g.blur.id, {middle.x - half.x, middle.y - half.y}, {middle.x + half.x, middle.y + half.y}, {0, 0}, {1, 1},
					IM_COL32(255, 255, 255, 170));
			}
			draw->AddRectFilledMultiColor({0, 0}, size, IM_COL32(5, 7, 13, 236), IM_COL32(5, 7, 13, 150), IM_COL32(5, 7, 13, 170),
				IM_COL32(5, 7, 13, 240));
			const ImVec2 origin{(size.x - 1920.0f * scale) * 0.5f, (size.y - 1080.0f * scale) * 0.5f};
			const ps5menu::Canvas canvas{draw, scale, origin, colours};
			// the cover, as the launcher's tile showed it, with a soft shadow
			const float coverHeight = 520;
			const float coverWidth = g.cover.id ? coverHeight * g.cover.width / g.cover.height : coverHeight * 0.75f;
			const float x0 = 200, y0 = (1080 - coverHeight) * 0.5f;
			for (int i = 0; i < 8; i++)
			{
				const float d = 4.0f + i * 5;
				draw->AddRectFilled(canvas.At(x0 - d, y0 - d * 0.5f), canvas.At(x0 + coverWidth + d, y0 + coverHeight + d * 1.5f),
					IM_COL32(0, 0, 0, 22 - i * 2), (22 + d) * scale);
			}
			if (g.cover.id)
				draw->AddImageRounded(g.cover.id, canvas.At(x0, y0), canvas.At(x0 + coverWidth, y0 + coverHeight), {0, 0}, {1, 1},
					IM_COL32(255, 255, 255, 255), 22 * scale);
			else
				draw->AddRectFilled(canvas.At(x0, y0), canvas.At(x0 + coverWidth, y0 + coverHeight), colours.accentInk, 22 * scale);
			draw->AddRect(canvas.At(x0, y0), canvas.At(x0 + coverWidth, y0 + coverHeight), IM_COL32(255, 255, 255, 30), 22 * scale, 0, 1.5f * scale);
			// beside it: the system, the game's name, GameTDB's publisher and year, then what Eden is doing
			std::string title, details;
			{
				std::lock_guard lock(s_mutex);
				title = s_title;
				details = s_details;
			}
			const float tx = x0 + coverWidth + 80, width = 1920 - tx - 180;
			float y = y0 + 40;
			canvas.Chip(g.chip, tx, y, "Nintendo Switch", colours.accentInk, colours.accent, true);
			y += 64;
			const ps5menu::Canvas::Lines lines = canvas.Break(g.big, 60, title, width);
			const int shown = std::min(2, (int)lines.drawn.size());
			for (int i = 0; i < shown; i++)
				canvas.Text(g.big, 60, tx, y + i * 70, colours.title, lines.drawn[i]);
			y += shown * 70 + 18;
			if (!details.empty())
				canvas.Text(g.body, ps5menu::kBody, tx, y, colours.copy, details);
			// at the cover's foot: the step, a bar for the shaders, and dots that breathe while it waits
			const float by = y0 + coverHeight - 64;
			canvas.Text(g.row, ps5menu::kLabel, tx, by, colours.text, StepText());
			const size_t of = s_shadersOf, done = s_shadersDone;
			const float barWidth = std::min(width, 560.0f);
			draw->AddRectFilled(canvas.At(tx, by + 44), canvas.At(tx + barWidth, by + 50), IM_COL32(255, 255, 255, 26), 3 * scale);
			if ((Step)s_step.load() == Step::Shaders && of > 0)
				draw->AddRectFilled(canvas.At(tx, by + 44), canvas.At(tx + barWidth * std::min(1.0f, (float)done / of), by + 50), colours.accent, 3 * scale);
			else
			{
				// a short light running along the bar
				const float run = std::fmod(time * 0.6f, 1.0f);
				const float a = std::max(0.0f, run * 1.3f - 0.3f), b = std::min(1.0f, run * 1.3f);
				draw->AddRectFilled(canvas.At(tx + barWidth * a, by + 44), canvas.At(tx + barWidth * b, by + 50), colours.accent, 3 * scale);
			}
			// the app's name, small, in the corner
			canvas.TextRight(g.small, ps5menu::kCaption, 1920 - 64, 1080 - 64, colours.copy, "Playtendoh");
		}

		// -- the menu --------------------------------------------------------------------------------

		std::string FilterName(int filter)
		{
			return Tr(kFilters[std::clamp(filter, 0, kFilterCount - 1)]);
		}

		std::string SpeedName(int index)
		{
			const int percent = kSpeedLimits[std::clamp(index, 0, 4)];
			// tr: no speed limit
			return percent ? Percent(percent) : std::string(TrC("speed limit", "None"));
		}

		std::string ControllerName(int controller)
		{
			return Tr(kControllers[std::clamp(controller + 1, 0, 5)]);
		}

		std::string AmiiboValue()
		{
			if (s_amiibo.empty())
				return Tr("None");
			const std::string& file = s_amiibo[s_amiiboSeries].files[s_amiiboIndex];
			const std::string name = ps5amiibo::FigureName(file);
			std::string answered;
			{
				std::lock_guard lock(s_mutex);
				answered = s_amiiboName;
			}
			// what came of the last one, while it is the one shown
			if (answered == file)
				switch ((AmiiboAnswer)s_amiiboAnswer.load())
				{
				// tr: {0} is an amiibo dump's file name
				case AmiiboAnswer::Touched: return TrF("Scanned {0}", name);
				// tr: the amiibo taken away from the controller, as from the Switch's NFC reader
				case AmiiboAnswer::Removed: return TrF("{0} taken away", name);
				case AmiiboAnswer::NotAsked: return Tr("The game is not asking for one");
				case AmiiboAnswer::NotAnAmiibo: return Tr("Not an amiibo dump");
				case AmiiboAnswer::Unreadable: return Tr("Could not be read");
				default: break;
				}
			return name;
		}

		std::vector<ps5menu::Row> MenuRows(const Settings& settings, bool exitAfterGame)
		{
			using Row = ps5menu::Row;
			const std::string on = Tr("On"), off = Tr("Off");
			std::vector<Row> rows;
			Row graphics{"graphics", Tr("Graphics"), FilterName(settings.filter), false,
				Tr("Upscaling, anti-aliasing, frame generation and the performance overlay. Kept for the next games."), {
				{"filter", Tr("Upscaling to 4K"), FilterName(settings.filter), true,
					Tr("How the picture is scaled to the TV: Bicubic is sharp, Bilinear softer, AMD FSR 1 keeps edges clean. It changes at once.")},
			}};
			// FSR 1's own setting, while it is the filter
			if (settings.filter == 1)
				graphics.rows.push_back({"sharpening", Tr("FSR sharpening"), SharpeningName(settings.sharpening), true,
					Tr("How much AMD FSR 1 sharpens the picture as it scales it: 100% is the most. It changes at once.")});
			graphics.rows.push_back({"aa", Tr("Anti-aliasing"), Tr(kAntiAliasing[std::clamp(settings.antiAliasing, 0, 2)]), true,
				Tr("Smooths the jagged edges of the game's picture: FXAA lightly, SMAA more cleanly, for a little more work by the GPU. It changes at once.")});
			if (settings.canGenerate)
				graphics.rows.push_back({"framegen", Tr("Frame generation"), settings.frameGeneration ? on : off, true,
					Tr("A picture made between each two frames of a game at 30 fps, so it is shown at 60. It changes at once.")});
			graphics.rows.push_back({"overlay", Tr("Performance overlay"), settings.overlay ? on : off, true,
				Tr("The frame rate and the emulation's speed, in a corner of the screen.")});
			rows.push_back(graphics);
			rows.push_back({"pace", Tr("Speed"), SpeedName(settings.speedLimit), false, Tr("How fast the game may run."), {
				{"speed", Tr("Speed limit"), SpeedName(settings.speedLimit), true,
					Tr("Above 100% hurries through slow scenes; None runs as fast as the PS5 can. For this game only.")},
			}});
			Row volume{"volume", Tr("Volume"), Percent(settings.volume), true, Tr("The game's sound. Left and Right change it by 10%.")};
			volume.slider = settings.volume / 100.0f;
			rows.push_back(volume);
			// tr: the emulated Switch's console mode: in its dock, or handheld
			const std::string mode = settings.docked ? TrC("switch mode", "Docked") : TrC("switch mode", "Handheld");
			rows.push_back({"controls", Tr("Controls"), fmt::format("{} · {}", mode, ControllerName(settings.controller)), false,
				Tr("Docked or handheld, the controller the game sees, vibration and the sticks' deadzone."), {
				{"docked", Tr("Console mode"), mode, true,
					Tr("Docked, the game draws for a TV, at its sharpest; handheld, for the console's own screen, which asks less of the GPU. The "
					   "game is told at once, as when a Switch leaves its dock.")},
				{"controller", Tr("Controller type"), ControllerName(settings.controller), true,
					Tr("What the game sees in your hands. Automatic is a Pro Controller; Handheld needs Handheld mode, and is player 1's alone. "
					   "Some games ask you to press a button to take the new one.")},
				{"vibration", Tr("Vibration"), settings.vibration ? on : off, true,
					Tr("The DualSense's rumble when the game asks for it. For every side, as Settings > Controls has it.")},
				{"deadzone", Tr("Stick deadzone"), Percent(settings.deadzone), true,
					Tr("How far a stick moves before the game sees it. Raise it if a character or the camera drifts.")},
			}});
			// tr: amiibo is Nintendo's name for its figures, as it writes it in the language
			Row amiibo{"amiibo", Tr("Amiibo"), AmiiboValue(), true,
				// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
				s_amiibo.empty() ? TrF("Put amiibo dumps (.bin) in {0}/amiibo to scan them here.", s_root) :
								   Tr("Left and Right choose an amiibo dump; Cross touches it to the controller when the game asks for one, "
									  "and again takes it away.")};
			if (s_amiibo.size() > 1)
				// dumps in folders, a series each: the series, then the figures in it
				rows.push_back({"amiibo-page", Tr("Amiibo"), amiibo.value, false, amiibo.help, {
					// tr: a folder of amiibo dumps, as they are shared: Kirby, Metroid...
					{"amiibo-series", Tr("Amiibo series"), ps5amiibo::SeriesName(s_amiibo[s_amiiboSeries]), true,
						TrF("Left and Right choose a series: a folder of dumps in {0}/amiibo.", s_root)},
					amiibo,
				}});
			else
				rows.push_back(amiibo);
			// the room joined for the game (the launcher's Multiplayer page), and who is in it
			Room room;
			{
				std::lock_guard lock(s_mutex);
				room = s_room;
			}
			if (room.joined || room.lost)
			{
				Row players{"room", Tr("Room"), room.lost ? std::string(Tr("Connection lost")) : room.name, false,
					Tr("The room joined for this game: choose Local Wireless (or local play) in the game to play with the others in it.")};
				for (size_t i = 0; i < room.members.size(); i++)
					players.rows.push_back({fmt::format("member{}", i), room.members[i], "", false, Tr("In the room.")});
				if (players.rows.empty())
					players.rows.push_back({"nomembers", Tr("Nobody else yet"), "", false, Tr("The players in the room show here as they join.")});
				else if (!room.lost)
					// tr: how many players are in the room
					players.value = fmt::format("{} · {}", room.name, ps5lang::TrP((int)room.members.size(), "{0} player", "{0} players"));
				rows.push_back(players);
			}
			// a home screen tile's game closes the app instead, as the row says (#48)
			Row leave = exitAfterGame ?
				Row{"leave", Tr("Quit to the home screen"), "", false,
					Tr("Leaves the game and closes Playtendoh, as its home screen tile asks. What you have not saved in the game is lost, so Cross is held.")} :
				Row{"leave", Tr("Quit to the library"), "", false,
					Tr("Leaves the game for the library. What you have not saved in the game is lost, so Cross is held.")};
			leave.apart = true;
			leave.hold = true;
			rows.push_back(leave);
			return rows;
		}

		// The quick actions above the list: back to the game, docked or handheld, and the amiibo chosen in
		// the list touched to the controller
		std::vector<ps5menu::Tile> Tiles(const Settings& settings)
		{
			// tr: the quick actions: big tiles at the top of the in-game menu, a word or two each
			return {{"resume", Tr("Resume"), "resume"},
				{"docked", Tr("Console mode"), "swap", settings.docked ? TrC("switch mode", "Docked") : TrC("switch mode", "Handheld")},
				{"amiibo", Tr("Amiibo"), "amiibo", s_amiibo.empty() ? Tr("None") : Tr("Scan")}};
		}

		void FindAmiibo()
		{
			s_amiibo = ps5amiibo::Find(s_root + "/amiibo");
			s_amiiboSeries = s_amiibo.empty() ? 0 : std::clamp(s_amiiboSeries, 0, (int)s_amiibo.size() - 1);
			s_amiiboIndex = s_amiibo.empty() ? 0 : std::clamp(s_amiiboIndex, 0, (int)s_amiibo[s_amiiboSeries].files.size() - 1);
		}

		// a step through count choices: Left and Right, or Cross onwards
		int Cycle(int index, int count, const ps5menu::Action& action)
		{
			return (std::clamp(index, 0, count - 1) + (action.change ? action.change : 1) + count) % count;
		}

		void Act(const ps5menu::Action& action, const Settings& settings)
		{
			const std::string& id = action.id;
			Settings next = settings;
			if (id == "resume")
			{
				CloseMenu();
				return;
			}
			if (id == "leave")
			{
				s_leave = true; // held to here
				CloseMenu();
				return;
			}
			if (id == "amiibo-series")
			{
				if (!s_amiibo.empty())
				{
					s_amiiboSeries = ps5amiibo::Step(s_amiiboSeries, action.change ? action.change : 1, (int)s_amiibo.size());
					s_amiiboIndex = 0;
				}
				return;
			}
			if (id == "amiibo")
			{
				if (s_amiibo.empty())
					return;
				const std::vector<std::string>& files = s_amiibo[s_amiiboSeries].files;
				if (!action.chosen)
				{
					s_amiiboIndex = ps5amiibo::Step(s_amiiboIndex, action.change, (int)files.size());
					return;
				}
				// the one shown, touched; touched again (it is still on the controller), taken away
				const std::string& name = files[s_amiiboIndex];
				std::lock_guard lock(s_mutex);
				const bool there = s_amiiboName == name && (AmiiboAnswer)s_amiiboAnswer.load() == AmiiboAnswer::Touched;
				s_amiiboRequest = there ? std::string() : s_root + "/amiibo/" + name;
				s_amiiboName = name;
				s_amiiboAnswer = -1;
				s_amiiboAsked = true;
				return;
			}
			if (id == "filter")
				next.filter = Cycle(settings.filter, kFilterCount, action);
			else if (id == "sharpening")
				next.sharpening = kSharpening[Cycle(SharpeningIndex(settings.sharpening), kSharpeningCount, action)];
			else if (id == "aa")
				next.antiAliasing = Cycle(settings.antiAliasing, 3, action);
			else if (id == "framegen")
				next.frameGeneration = !settings.frameGeneration;
			else if (id == "overlay")
				next.overlay = !settings.overlay;
			else if (id == "speed")
				next.speedLimit = Cycle(settings.speedLimit, 5, action);
			else if (id == "volume")
				next.volume = action.chosen && settings.volume >= 100 ? 0 : std::clamp(settings.volume + action.change * 10, 0, 100);
			else if (id == "docked")
			{
				// the handheld controller is not played docked: a Pro Controller then
				next.docked = !settings.docked;
				if (next.docked && settings.controller == kHandheld)
					next.controller = -1;
			}
			else if (id == "controller")
			{
				next.controller = Cycle(settings.controller + 1, 6, action) - 1;
				if (next.controller == kHandheld)
					next.docked = false;
			}
			else if (id == "vibration")
				next.vibration = !settings.vibration;
			else if (id == "deadzone")
				next.deadzone = action.chosen && settings.deadzone >= kDeadzoneMost ? 0 :
					std::clamp(settings.deadzone + (action.change ? action.change : 1) * kDeadzoneStep, 0, kDeadzoneMost);
			else
				return;
			std::lock_guard lock(s_mutex);
			s_settings = next;
			s_changed = true;
			s_everChanged = true;
		}

		bool s_exitAfterGame = false;

		void DrawMenu(float scale)
		{
			Settings settings;
			std::string title, details;
			{
				std::lock_guard lock(s_mutex);
				settings = s_settings;
				title = s_title;
				details = s_details;
			}
			if (s_menuFresh.exchange(false))
			{
				s_side.Reset();
				FindAmiibo();
			}
			bool close = false;
			const ps5menu::Action action = s_side.Update(MenuRows(settings, s_exitAfterGame), Tiles(settings), s_menuButtons, Now(), close);
			if (close)
			{
				CloseMenu();
				return;
			}
			if (!action.id.empty())
			{
				Act(action, settings);
				std::lock_guard lock(s_mutex);
				settings = s_settings;
			}
			ImGuiIO& io = ImGui::GetIO();
			const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
			const ps5menu::Canvas canvas{ImGui::GetForegroundDrawList(), scale, origin, ps5menu::kRed};
			ps5menu::Header header;
			header.system = "Switch";
			header.status = Tr("Running"); // the game runs on behind the menu, as Cemu's does (ingame.h)
			header.title = title;
			header.details = details;
			header.cover = g.cover.id;
			header.coverWidth = (float)g.cover.width;
			header.coverHeight = (float)g.cover.height;
			s_side.Draw(canvas, {g.head, g.body, g.row, g.small, g.chip}, header, MenuRows(settings, s_exitAfterGame), Tiles(settings),
				{{"cross", Tr("Choose")}, {"leftright", Tr("Change")}, {"circle", s_side.Open().empty() ? Tr("Back to the game") : Tr("Back")}});
		}

		// The controller as the menu reads it: the left stick's directions as the D-pad's
		void Input()
		{
			uint32_t buttons = s_buttons;
			const uint8_t x = s_leftX, y = s_leftY;
			if (y < 64)
				buttons |= ps5pad::kUp;
			else if (y > 192)
				buttons |= ps5pad::kDown;
			if (x < 64)
				buttons |= ps5pad::kLeft;
			else if (x > 192)
				buttons |= ps5pad::kRight;
			s_menuButtons = buttons & ps5pad::kTouchPad ? 0 : buttons; // a shortcut's buttons are not the menu's
			ImGuiIO& io = ImGui::GetIO();
			io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
			io.MouseDown[0] = false;
		}
	}

	void Start(const Settings& settings)
	{
		std::ifstream in(SessionFile());
		std::string title, details, cover, language, font, game, root = ps5paths::kDefaultRoot;
		int face = 0;
		bool exitAfterGame = false;
		for (std::string line; std::getline(in, line);)
		{
			const size_t equals = line.find('=');
			if (equals == std::string::npos)
				continue;
			const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
			if (key == "title")
				title = value;
			else if (key == "details")
				details = value;
			else if (key == "cover")
				cover = value;
			else if (key == "language")
				language = value;
			else if (key == "font")
				font = value;
			else if (key == "face")
				face = std::atoi(value.c_str());
			else if (key == "exit")
				exitAfterGame = value == "1";
			else if (key == "game")
				game = value;
			else if (key == "root" && !value.empty())
				root = value;
		}
		const bool session = in.is_open() && !title.empty();
		in.close();
		std::remove(SessionFile().c_str()); // this game's: the next one leaves its own
		ps5lang::Load(language);
		ps5lang::SetMenuFont({font, face});
		{
			std::lock_guard lock(s_mutex);
			s_title = title;
			s_details = details;
			s_coverPath = cover;
			s_game = game;
			s_root = root;
			s_session = session;
			s_settings = settings;
			s_everChanged = false;
			s_exitAfterGame = exitAfterGame;
			s_loading = session;
		}
		ps5log::Line("[ingamenx] {}: {}", session ? title : std::string("no session file"),
			session ? "Playtendoh's loading screen and menu" : "Eden's own loading screen, no menu");
		// from here until Eden's program says otherwise, a session that never ends was a crash
		if (session)
			WriteResult("running");
	}

	void WriteResult(const char* state, const std::string& error)
	{
		Settings settings;
		std::string game;
		bool changed;
		{
			std::lock_guard lock(s_mutex);
			settings = s_settings;
			game = s_game;
			changed = s_everChanged;
		}
		const std::string file = ResultFile(), staged = file + ".new";
		{
			std::ofstream out(staged, std::ios::trunc);
			std::string why = error;
			std::replace(why.begin(), why.end(), '\n', ' ');
			out << "state=" << state << '\n';
			if (!why.empty())
				out << "error=" << why << '\n';
			if (!game.empty())
				out << "game=" << game << '\n';
			if (changed)
				out << "changed=1\nfilter=" << settings.filter << "\nantiAliasing=" << settings.antiAliasing << "\nsharpening="
					<< settings.sharpening << "\nframeGeneration=" << settings.frameGeneration << "\noverlay=" << settings.overlay
					<< "\nspeedLimit=" << settings.speedLimit << "\nvolume=" << settings.volume << "\ndocked=" << settings.docked
					<< "\ncontroller=" << settings.controller << "\nvibration=" << settings.vibration << "\ndeadzone=" << settings.deadzone
					<< '\n';
			if (!out)
			{
				ps5log::Line("[ingamenx] {} could not be written", staged);
				return;
			}
		}
		std::rename(staged.c_str(), file.c_str());
		ps5log::Line("[ingamenx] result: {}{}", state, error.empty() ? std::string() : ": " + error);
	}

	void SetRoom(const Room& room)
	{
		std::lock_guard lock(s_mutex);
		s_room = room;
	}

	bool TakeAmiiboRequest(std::string& path)
	{
		std::lock_guard lock(s_mutex);
		if (!s_amiiboAsked)
			return false;
		s_amiiboAsked = false;
		path = s_amiiboRequest;
		return true;
	}

	void SetAmiiboAnswer(AmiiboAnswer answer)
	{
		s_amiiboAnswer = (int)answer;
		ps5log::Line("[ingamenx] amiibo: answer {}", (int)answer);
	}

	void SetLoading(Step step)
	{
		s_step = (int)step;
	}

	void SetShaders(size_t done, size_t of)
	{
		s_shadersDone = done;
		s_shadersOf = of;
	}

	void EndLoading()
	{
		s_loading = false;
		s_drawsLoading = false;
	}

	bool DrawsLoading()
	{
		return s_drawsLoading;
	}

	bool Controller(uint32_t buttons, uint8_t leftX, uint8_t leftY)
	{
		s_buttons = buttons;
		s_leftX = leftX;
		s_leftY = leftY;
		if (s_open)
			return true;
		if (s_release && buttons == 0)
			s_release = false;
		return s_release;
	}

	void ToggleMenu()
	{
		if (!s_ready || s_loading)
			return; // nothing to draw it with yet, or the game is still loading
		const bool open = !s_open;
		if (open)
		{
			s_menuFresh = true;
			s_open = true;
		}
		else
			CloseMenu();
		ps5log::Line("[ingamenx] the menu {}", open ? "opens" : "closes");
	}

	bool MenuOpen()
	{
		return s_open || s_release;
	}

	bool TakeChanges(Settings& settings)
	{
		std::lock_guard lock(s_mutex);
		if (!s_changed)
			return false;
		s_changed = false;
		settings = s_settings;
		return true;
	}

	bool TakeLeaveRequest()
	{
		return s_leave.exchange(false);
	}

	void Record(const Target& target)
	{
		if (g.failed)
			return;
		if (!target.insideRenderPass)
		{
			g.pending = false;
			bool session;
			{
				std::lock_guard lock(s_mutex);
				session = s_session;
			}
			if (!session)
				return; // started without the launcher's word: Eden's own screens, no menu
			const bool loading = s_loading;
			const bool open = s_open;
			// made ready at the first frame, loading screen or not: the menu needs it later
			if (!g.ready)
			{
				if (!Initialize(target, target.height / 1080.0f))
				{
					g.failed = true;
					s_drawsLoading = false;
					return;
				}
				g.ready = true;
				s_ready = true;
			}
			// Eden makes its window pass again when the filter or the swapchain's image count changes (the
			// game's first frame after the loading screen, a filter chosen in the menu). Each one has the
			// same single attachment, in the swapchain's format, so ImGui's pipeline draws in any of them
			if (target.renderPass != g.target.renderPass || target.width != g.target.width || target.height != g.target.height)
			{
				ps5log::Line("[ingamenx] Eden's window pass made again ({}x{}): drawn in the new one", target.width, target.height);
				g.target.renderPass = target.renderPass;
				g.target.width = target.width;
				g.target.height = target.height;
			}
			ImGui::SetCurrentContext(g.context);
			if (!g.fontsUploaded)
			{
				ImGui_ImplVulkan_CreateFontsTexture(target.commandBuffer);
				g.fontsUploaded = true;
				g.uploadAge = 0;
			}
			else if (g.uploadAge >= 0 && ++g.uploadAge > 16)
			{
				ImGui_ImplVulkan_DestroyFontUploadObjects();
				g.uploadAge = -1;
			}
			if (!g.picturesTried)
				MakePictures(target.commandBuffer);
			if (!loading && !open)
				return; // made ready for the menu, nothing to draw
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = {(float)target.width, (float)target.height};
			static uint64_t last = 0;
			static const uint64_t first = Now();
			const uint64_t now = Now();
			io.DeltaTime = last && now > last ? std::min((now - last) / 1e6f, 0.25f) : 1.0f / 60.0f;
			last = now;
			Input();
			ImGui_ImplVulkan_NewFrame();
			ImGui::NewFrame();
			const float drawScale = target.height / 1080.0f;
			if (loading)
			{
				DrawLoading(drawScale, (now - first) / 1e6f);
				s_drawsLoading = true;
			}
			else if (open)
				DrawMenu(drawScale);
			ImGui::Render();
			g.pending = true;
			return;
		}
		if (!g.pending || target.renderPass != g.target.renderPass)
			return;
		g.pending = false;
		ImGui::SetCurrentContext(g.context);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), target.commandBuffer);
	}
}
