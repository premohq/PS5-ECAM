// SPDX-License-Identifier: GPL-3.0-or-later
#include "pad.h"
#include "kernel.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

extern "C"
{
	struct LoginUserIdList
	{
		int32_t userId[4]; // unused entries are -1
	};
	struct PadControllerInformation
	{
		float touchPixelDensity;
		uint16_t touchResolutionX, touchResolutionY;
		uint8_t deadZoneLeft, deadZoneRight;
		uint8_t connectionType, connectedCount;
		int32_t connected;
		int32_t deviceClass;
		uint8_t reserved[8];
	};
	struct PadVibration
	{
		uint8_t largeMotor, smallMotor;
	};
	struct PadColor
	{
		uint8_t r, g, b, reserved;
	};

	int32_t sceUserServiceInitialize(void* parameters);
	int32_t sceUserServiceGetForegroundUser(int32_t* userId);
	int32_t sceUserServiceGetInitialUser(int32_t* userId);
	int32_t sceUserServiceGetLoginUserIdList(LoginUserIdList* list);
	int32_t sceUserServiceGetUserName(int32_t userId, char* name, size_t size);
	int32_t scePadInit();
	int32_t scePadOpen(int32_t userId, int32_t type, int32_t index, const void* parameters);
	int32_t scePadGetHandle(int32_t userId, int32_t type, int32_t index);
	int32_t scePadClose(int32_t handle);
	int32_t scePadReadState(int32_t handle, ps5pad::Data* data);
	int32_t scePadGetControllerInformation(int32_t handle, PadControllerInformation* information);
	int32_t scePadSetVibration(int32_t handle, const PadVibration* vibration);
	int32_t scePadSetLightBar(int32_t handle, const PadColor* color);
	int32_t scePadSetMotionSensorState(int32_t handle, bool enabled);
	int32_t scePadSetVibrationMode(int32_t handle, int32_t mode);
}
static_assert(sizeof(PadControllerInformation) == 28);

namespace ps5pad
{
	namespace
	{
		constexpr int32_t kPortTypeStandard = 0;
		// the two motors scePadSetVibration drives; the pad opens in haptics mode, where they do
		// nothing (#41, found and tested on a console by 3lmerx503)
		constexpr int32_t kVibrationModeCompatible = 2;

		struct Slot
		{
			int32_t user = -1;
			int32_t handle = -1;
			float touchWidth = 1920, touchHeight = 1080;
			uint32_t lastButtons = 0;
			// the touchpad's click: when it began, whether it made a shortcut or touched the
			// GamePad's screen, and until when a short click's tap lasts
			uint64_t clickedAt = 0, tapUntil = 0;
			bool chordUsed = false, touched = false;
			uint64_t loadAt = 0; // Triangle pressed with the click, for the load state shortcut: when (0: none)
			uint64_t filteredAt = 0; // the last sample FilterShortcuts saw (KeepShortcuts)
		};

		std::mutex s_mutex;
		std::array<Slot, kMaxPlayers> s_slots;
		bool s_initialized = false;
		std::atomic<bool> s_vibrationEnabled{true};
		std::atomic<Shortcut> s_pendingShortcut{Shortcut::None};
		std::array<std::atomic<bool>, kMaxPlayers> s_touchpadButton{};
		std::atomic<uint32_t> s_turbo{0};
		std::atomic<bool> s_stateShortcuts{false};

		void OpenSlot(int player, int32_t user)
		{
			int32_t handle = scePadOpen(user, kPortTypeStandard, 0, nullptr);
			if (handle < 0) // already open in this process
				handle = scePadGetHandle(user, kPortTypeStandard, 0);
			if (handle < 0)
			{
				// said once a user: Rescan tries again every two seconds
				static std::vector<int32_t> s_told;
				if (std::find(s_told.begin(), s_told.end(), user) == s_told.end())
				{
					s_told.push_back(user);
					ps5log::Line("[pad] no controller for user {:#x} (player {}): {:#x}", user, player + 1, (uint32_t)handle);
				}
				return;
			}
			Slot& slot = s_slots[player];
			slot = {};
			slot.user = user;
			slot.handle = handle;
			PadControllerInformation info{};
			if (scePadGetControllerInformation(handle, &info) == 0 && info.touchResolutionX && info.touchResolutionY)
			{
				slot.touchWidth = info.touchResolutionX;
				slot.touchHeight = info.touchResolutionY;
			}
			scePadSetMotionSensorState(handle, true);
			if (int32_t result = scePadSetVibrationMode(handle, kVibrationModeCompatible); result < 0)
				ps5log::Line("[pad] player {}'s vibration mode could not be set: {:#x}", player + 1, (uint32_t)result);
			ps5log::Line("[pad] player {} is user {:#x} (handle {})", player + 1, user, handle);
		}

		void CloseSlot(int player)
		{
			Slot& slot = s_slots[player];
			if (slot.handle >= 0)
				scePadClose(slot.handle);
			slot = {};
		}
	}

	bool Init()
	{
		std::lock_guard lock(s_mutex);
		if (s_initialized)
			return s_slots[0].handle >= 0;
		sceUserServiceInitialize(nullptr);
		if (scePadInit() < 0)
		{
			ps5log::Line("[pad] scePadInit failed");
			return false;
		}
		s_initialized = true;
		int32_t user = -1;
		if (sceUserServiceGetForegroundUser(&user) < 0 || user < 0)
			sceUserServiceGetInitialUser(&user);
		if (user >= 0)
			OpenSlot(0, user);
		return s_slots[0].handle >= 0;
	}

	void Shutdown()
	{
		std::lock_guard lock(s_mutex);
		for (int player = 0; player < kMaxPlayers; player++)
			CloseSlot(player);
	}

	void Rescan()
	{
		std::lock_guard lock(s_mutex);
		if (!s_initialized)
			return;
		LoginUserIdList list;
		std::fill(std::begin(list.userId), std::end(list.userId), -1);
		if (sceUserServiceGetLoginUserIdList(&list) < 0)
			return;
		auto signedIn = [&](int32_t user) { return std::find(std::begin(list.userId), std::end(list.userId), user) != std::end(list.userId); };
		// player 1 stays with the user who started PS5Cemu; the others leave when they sign out
		for (int player = 1; player < kMaxPlayers; player++)
			if (s_slots[player].handle >= 0 && !signedIn(s_slots[player].user))
				CloseSlot(player);
		for (int32_t user : list.userId)
		{
			if (user < 0 || std::any_of(s_slots.begin(), s_slots.end(), [user](const Slot& s) { return s.handle >= 0 && s.user == user; }))
				continue;
			for (int player = 0; player < kMaxPlayers; player++)
			{
				if (s_slots[player].handle < 0)
				{
					OpenSlot(player, user);
					break;
				}
			}
		}
	}

	bool Read(int player, Data& out)
	{
		int32_t handle;
		{
			std::lock_guard lock(s_mutex);
			if (player < 0 || player >= kMaxPlayers || s_slots[player].handle < 0)
				return false;
			handle = s_slots[player].handle;
		}
		if (scePadReadState(handle, &out) < 0 || !out.connected)
			return false;
		// once for each controller, the bytes the state has that no header describes (its device data and
		// extension): where the DualSense's battery is, for a corner of the launcher to show it (roadmap L8)
		static std::atomic<bool> s_described[kMaxPlayers]{};
		if (!s_described[player].exchange(true))
		{
			std::string device, extension;
			for (int i = 0; i < std::min<int>(out.deviceUniqueDataLength, sizeof(out.deviceUniqueData)); i++)
				device += fmt::format("{:02x}", out.deviceUniqueData[i]);
			for (uint8_t byte : out.extension)
				extension += fmt::format("{:02x}", byte);
			ps5log::Line("[pad] P{}: device data ({} bytes) {}, extension {}", player + 1, (int)out.deviceUniqueDataLength,
				device.empty() ? "none" : device, extension);
		}
		if (out.buttons & kIntercepted)
		{
			// the system menu has the controller: report it at rest
			out.buttons = 0;
			out.leftX = out.leftY = out.rightX = out.rightY = 128;
			out.l2 = out.r2 = 0;
			out.touchCount = 0;
		}
		return true;
	}

	bool IsConnected(int player)
	{
		Data data;
		return Read(player, data);
	}

	int32_t UserId(int player)
	{
		std::lock_guard lock(s_mutex);
		return player >= 0 && player < kMaxPlayers && s_slots[player].handle >= 0 ? s_slots[player].user : -1;
	}

	std::string UserName(int player)
	{
		const int32_t user = UserId(player);
		char name[32] = {};
		if (user < 0 || sceUserServiceGetUserName(user, name, sizeof(name) - 1) != 0)
			return {};
		return name;
	}

	void TouchResolution(int player, float& width, float& height)
	{
		std::lock_guard lock(s_mutex);
		const Slot& slot = s_slots[std::clamp(player, 0, kMaxPlayers - 1)];
		width = slot.touchWidth;
		height = slot.touchHeight;
	}

	void SetVibration(int player, uint8_t largeMotor, uint8_t smallMotor)
	{
		std::lock_guard lock(s_mutex);
		if (player < 0 || player >= kMaxPlayers || s_slots[player].handle < 0)
			return;
		const bool enabled = s_vibrationEnabled;
		const PadVibration vibration{enabled ? largeMotor : (uint8_t)0, enabled ? smallMotor : (uint8_t)0};
		scePadSetVibration(s_slots[player].handle, &vibration);
	}

	void SetVibrationEnabled(bool enabled)
	{
		s_vibrationEnabled = enabled;
	}

	void SetLightBar(int player, uint8_t r, uint8_t g, uint8_t b)
	{
		std::lock_guard lock(s_mutex);
		if (player < 0 || player >= kMaxPlayers || s_slots[player].handle < 0)
			return;
		const PadColor color{r, g, b, 0};
		scePadSetLightBar(s_slots[player].handle, &color);
	}

	Filtered FilterShortcuts(int player, uint32_t buttons)
	{
		constexpr uint64_t kTouchDelayUs = 150000; // a shortcut's button within this is no touch
		constexpr uint64_t kTapUs = 80000;		   // how long a shorter click touches, once released
		constexpr uint64_t kLoadHoldUs = 600000;   // Triangle held this long loads: a load loses what was played since
		const uint64_t now = sceKernelGetProcessTime();
		std::lock_guard lock(s_mutex);
		Slot& slot = s_slots[std::clamp(player, 0, kMaxPlayers - 1)];
		const uint32_t previous = slot.lastButtons;
		slot.lastButtons = buttons;
		slot.filteredAt = now;
		auto pressed = [&](uint32_t mask) { return (buttons & mask) && !(previous & mask); };
		Filtered result{buttons & ~kTouchPad, false};
		if (buttons & kTouchPad)
		{
			if (!(previous & kTouchPad))
			{
				slot.clickedAt = now;
				slot.chordUsed = slot.touched = false;
			}
			Shortcut shortcut = pressed(kOptions) ? Shortcut::Menu
				: pressed(kL1)					  ? Shortcut::SwapScreens
				: pressed(kR1)					  ? Shortcut::CornerScreen
												  : Shortcut::None;
			if (s_stateShortcuts)
			{
				if (shortcut == Shortcut::None && pressed(kSquare))
					shortcut = Shortcut::SaveState;
				if (!(buttons & kTriangle))
					slot.loadAt = 0;
				else if (pressed(kTriangle))
				{
					slot.loadAt = now;
					slot.chordUsed = true; // no touch while it is held
				}
				else if (shortcut == Shortcut::None && slot.loadAt && now - slot.loadAt >= kLoadHoldUs)
				{
					shortcut = Shortcut::LoadState;
					slot.loadAt = 0; // once a hold
				}
				result.buttons &= ~(kSquare | kTriangle);
			}
			if (shortcut != Shortcut::None)
			{
				s_pendingShortcut = shortcut;
				slot.chordUsed = true;
			}
			result.buttons &= ~(kOptions | kL1 | kR1);
			result.touch = !slot.chordUsed && now - slot.clickedAt >= kTouchDelayUs;
			slot.touched |= result.touch;
		}
		else
		{
			slot.loadAt = 0;
			if ((previous & kTouchPad) && !slot.chordUsed && !slot.touched)
				slot.tapUntil = now + kTapUs;
		}
		if (now < slot.tapUntil)
			result.touch = true;
		// a button instead, for a player whose click is one: pressed when the touch would be
		const int index = std::clamp(player, 0, kMaxPlayers - 1);
		if (s_touchpadButton[index])
		{
			result.touchClick = result.touch;
			result.touch = false;
		}
		return result;
	}

	void KeepShortcuts(int player)
	{
		constexpr uint64_t kQuietUs = 100000; // a game reads its controllers every frame or two
		if (player < 0 || player >= kMaxPlayers)
			return;
		{
			std::lock_guard lock(s_mutex);
			if (sceKernelGetProcessTime() - s_slots[player].filteredAt < kQuietUs)
				return;
		}
		Data data;
		if (Read(player, data))
			FilterShortcuts(player, data.buttons);
	}

	Shortcut TakeShortcut()
	{
		return s_pendingShortcut.exchange(Shortcut::None);
	}

	void SetStateShortcuts(bool on)
	{
		s_stateShortcuts = on;
	}

	void SetTouchpadButton(int player, bool button)
	{
		if (player >= 0 && player < kMaxPlayers)
			s_touchpadButton[player] = button;
	}

	bool TouchpadButton(int player)
	{
		return player >= 0 && player < kMaxPlayers && s_touchpadButton[player];
	}

	void SetTurbo(uint32_t buttons)
	{
		s_turbo = buttons;
	}

	uint32_t ButtonNamed(const std::string& name)
	{
		static constexpr std::pair<const char*, uint32_t> kNames[] = {
			{"Cross", kCross}, {"Circle", kCircle}, {"Square", kSquare}, {"Triangle", kTriangle}, {"L1", kL1}, {"R1", kR1},
			{"L2", kL2}, {"R2", kR2}, {"L3", kL3}, {"R3", kR3}, {"Create", kCreate}, {"Options", kOptions},
			{"D-pad up", kUp}, {"D-pad down", kDown}, {"D-pad left", kLeft}, {"D-pad right", kRight},
		};
		for (const auto& [text, bit] : kNames)
			if (name == text)
				return bit;
		return 0;
	}

	uint32_t ApplyTurbo(uint32_t buttons)
	{
		const uint32_t turbo = s_turbo.load(std::memory_order_relaxed);
		if (!(buttons & turbo))
			return buttons;
		// down for 33 ms, up for 33 ms: about fifteen presses a second, which games read as presses
		constexpr uint64_t kBeatUs = 33333;
		const bool up = (sceKernelGetProcessTime() / kBeatUs) & 1;
		return up ? buttons & ~turbo : buttons;
	}
}
