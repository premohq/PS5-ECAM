// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include "../app/paths.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace ps5settings
{
	namespace
	{
		using Writer = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

		std::mutex s_brokenMutex; // Load runs on the remote touch screen's threads too (its upload folders)
		std::string s_broken;	  // where Load moved a ps5cemu.json it could not read (TakeBrokenFile)

		std::string Hex(uint64_t value)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)value);
			return text;
		}

		void ReadString(const rapidjson::Value& json, const char* key, std::string& out)
		{
			if (json.HasMember(key) && json[key].IsString())
				out = json[key].GetString();
		}

		void ReadInt(const rapidjson::Value& json, const char* key, int& out, int low, int high)
		{
			if (json.HasMember(key) && json[key].IsInt())
				out = std::clamp(json[key].GetInt(), low, high);
		}

		void ReadBool(const rapidjson::Value& json, const char* key, bool& out)
		{
			if (json.HasMember(key) && json[key].IsBool())
				out = json[key].GetBool();
		}

		void ReadGames(const rapidjson::Value& json, uint64_t& lastGame, std::vector<uint64_t>& recent)
		{
			if (json.HasMember("lastGame") && json["lastGame"].IsString())
				lastGame = std::strtoull(json["lastGame"].GetString(), nullptr, 16);
			if (json.HasMember("recent") && json["recent"].IsArray())
				for (const auto& entry : json["recent"].GetArray())
					if (entry.IsString() && recent.size() < 4)
						recent.push_back(std::strtoull(entry.GetString(), nullptr, 16));
		}

		void WriteGames(Writer& writer, uint64_t lastGame, const std::vector<uint64_t>& recent)
		{
			writer.Key("lastGame");
			writer.String(Hex(lastGame).c_str());
			writer.Key("recent");
			writer.StartArray();
			for (uint64_t titleId : recent)
				writer.String(Hex(titleId).c_str());
			writer.EndArray();
		}

		// relocate: a folder in the data folder follows it to a drive (the Switch's does not: Eden's
		// program resolves /data/eden or its legacy /data/ps5cemu/eden wherever ours is)
		void ReadN3ds(const rapidjson::Value& json, N3ds& n3ds, bool relocate = true)
		{
			ReadInt(json, "retroBackend", n3ds.retroBackend, 0, 1);
			ReadString(json, "gamesFolder", n3ds.gamesFolder);
			if (relocate)
				n3ds.gamesFolder = ps5paths::Relocated(n3ds.gamesFolder); // saved before the data moved to a drive (#52)
			n3ds.gamesFolder = ps5paths::Moved(n3ds.gamesFolder); // or before the side had a folder of its own (app/reorganize.h)
			ReadInt(json, "resolution", n3ds.resolution, 1, 10);
			ReadInt(json, "layout", n3ds.layout, 0, 3);
			ReadInt(json, "textureFilter", n3ds.textureFilter, 0, 5);
			ReadInt(json, "volume", n3ds.volume, 0, 100);
			ReadBool(json, "motion", n3ds.motion);
			ReadInt(json, "deadzone", n3ds.deadzone, 0, 50);
			ReadBool(json, "performance", n3ds.performance);
			ReadInt(json, "cpuClock", n3ds.cpuClock, 25, 400);
			ReadBool(json, "tscClock", n3ds.tscClock);
			ReadBool(json, "invariantPosition", n3ds.invariantPosition);
			ReadBool(json, "hwShaders", n3ds.hwShaders);
			ReadBool(json, "anaglyph", n3ds.anaglyph);
			ReadString(json, "turbo", n3ds.turbo);
			ReadInt(json, "region", n3ds.region, -1, 6);
			ReadInt(json, "language", n3ds.language, -1, 11);
			ReadBool(json, "customTextures", n3ds.customTextures);
			ReadBool(json, "asyncShaderCompilation", n3ds.asyncShaders);
			ReadInt(json, "border", n3ds.border, 0, 5);
			ReadInt(json, "dsFilter", n3ds.dsFilter, 0, 2);
			ReadInt(json, "dsRotation", n3ds.dsRotation, 0, 3);
			ReadBool(json, "screenGap", n3ds.screenGap);
			ReadBool(json, "hiddenTouch", n3ds.hiddenTouch);
			ReadBool(json, "frameGeneration", n3ds.frameGeneration);
			ReadBool(json, "dsJit", n3ds.dsJit);
			ReadBool(json, "dsOwnBios", n3ds.dsOwnBios);
			ReadBool(json, "dsSlot2", n3ds.dsSlot2);
			ReadBool(json, "dsStylus", n3ds.dsStylus);
			ReadInt(json, "dsMicSource", n3ds.dsMicSource, 0, 2);
			ReadInt(json, "dsTouchX", n3ds.dsTouchX, -20, 20);
			ReadInt(json, "dsTouchY", n3ds.dsTouchY, -20, 20);
			ReadInt(json, "dsTouchScale", n3ds.dsTouchScale, 50, 150);
			ReadInt(json, "nxResolution", n3ds.nxResolution, 0, 6);
			ReadInt(json, "nxFilter", n3ds.nxFilter, 0, 3);
			ReadInt(json, "nxAstc", n3ds.nxAstc, 0, 1);
			ReadBool(json, "nxDocked", n3ds.nxDocked);
			ReadBool(json, "nxFrameGeneration", n3ds.nxFrameGeneration);
			ReadInt(json, "nxSpeedLimit", n3ds.nxSpeedLimit, 0, 4);
			ReadInt(json, "nxCovers", n3ds.nxCovers, 0, 1);
			ReadInt(json, "nxAntiAliasing", n3ds.nxAntiAliasing, 0, 2);
			ReadInt(json, "nxSharpening", n3ds.nxSharpening, 0, 200);
			ReadInt(json, "nxController", n3ds.nxController, -1, 4);
			ReadInt(json, "nxDeadzone", n3ds.nxDeadzone, 0, 30);
			ReadInt(json, "nxOutput", n3ds.nxOutput, 0, 2);
			ReadBool(json, "nxHighRefresh", n3ds.nxHighRefresh);
			ReadInt(json, "nxLanguage", n3ds.nxLanguage, -1, 17);
			ReadInt(json, "nxMemory", n3ds.nxMemory, 0, 2);
			ReadBool(json, "nxAsyncShaders", n3ds.nxAsyncShaders);
			ReadBool(json, "nxFastGpu", n3ds.nxFastGpu);
			ReadBool(json, "nxUnsafeCpu", n3ds.nxUnsafeCpu);
			ReadBool(json, "nxUnsafeDma", n3ds.nxUnsafeDma);
			ReadBool(json, "nxReactiveFlushing", n3ds.nxReactiveFlushing);
			ReadBool(json, "nxSkipInvalidation", n3ds.nxSkipInvalidation);
			ReadBool(json, "nxBlockList", n3ds.nxBlockList);
			ReadBool(json, "nxCompactMemory", n3ds.nxCompactMemory);
			ReadString(json, "nxRoomHost", n3ds.nxRoomHost);
			ReadInt(json, "nxRoomPort", n3ds.nxRoomPort, 1, 65535);
			ReadString(json, "nxRoomName", n3ds.nxRoomName);
			ReadString(json, "nxRoomPassword", n3ds.nxRoomPassword);
			ReadString(json, "nxNickname", n3ds.nxNickname);
			ReadString(json, "articAddress", n3ds.articAddress);
			ReadInt(json, "gameCount", n3ds.gameCount, -1, 1000000);
			if (json.HasMember("buttons") && json["buttons"].IsObject())
				for (const auto& member : json["buttons"].GetObject())
					if (member.value.IsString())
						n3ds.buttons[member.name.GetString()] = member.value.GetString();
			ReadGames(json, n3ds.lastGame, n3ds.recent);
		}

		void WriteN3ds(Writer& writer, const N3ds& n3ds)
		{
			writer.StartObject();
			writer.Key("retroBackend"); writer.Int(n3ds.retroBackend);
			writer.Key("gamesFolder");
			writer.String(n3ds.gamesFolder.c_str());
			writer.Key("resolution");
			writer.Int(n3ds.resolution);
			writer.Key("layout");
			writer.Int(n3ds.layout);
			writer.Key("textureFilter");
			writer.Int(n3ds.textureFilter);
			writer.Key("volume");
			writer.Int(n3ds.volume);
			writer.Key("motion");
			writer.Bool(n3ds.motion);
			writer.Key("deadzone");
			writer.Int(n3ds.deadzone);
			writer.Key("performance");
			writer.Bool(n3ds.performance);
			writer.Key("cpuClock");
			writer.Int(n3ds.cpuClock);
			writer.Key("tscClock");
			writer.Bool(n3ds.tscClock);
			writer.Key("invariantPosition");
			writer.Bool(n3ds.invariantPosition);
			writer.Key("hwShaders");
			writer.Bool(n3ds.hwShaders);
			writer.Key("anaglyph");
			writer.Bool(n3ds.anaglyph);
			writer.Key("turbo");
			writer.String(n3ds.turbo.c_str());
			writer.Key("region");
			writer.Int(n3ds.region);
			writer.Key("language");
			writer.Int(n3ds.language);
			writer.Key("customTextures");
			writer.Bool(n3ds.customTextures);
			writer.Key("asyncShaderCompilation");
			writer.Bool(n3ds.asyncShaders);
			writer.Key("border");
			writer.Int(n3ds.border);
			writer.Key("dsFilter");
			writer.Int(n3ds.dsFilter);
			writer.Key("dsRotation");
			writer.Int(n3ds.dsRotation);
			writer.Key("screenGap");
			writer.Bool(n3ds.screenGap);
			writer.Key("hiddenTouch");
			writer.Bool(n3ds.hiddenTouch);
			writer.Key("frameGeneration");
			writer.Bool(n3ds.frameGeneration);
			writer.Key("dsJit");
			writer.Bool(n3ds.dsJit);
			writer.Key("dsOwnBios");
			writer.Bool(n3ds.dsOwnBios);
			writer.Key("dsTouchX"); writer.Int(n3ds.dsTouchX);
			writer.Key("dsTouchY"); writer.Int(n3ds.dsTouchY);
			writer.Key("dsTouchScale"); writer.Int(n3ds.dsTouchScale);
			writer.Key("dsMicSource"); writer.Int(n3ds.dsMicSource);
			writer.Key("dsSlot2");
			writer.Bool(n3ds.dsSlot2);
			writer.Key("dsStylus");
			writer.Bool(n3ds.dsStylus);
			writer.Key("nxResolution");
			writer.Int(n3ds.nxResolution);
			writer.Key("nxFilter");
			writer.Int(n3ds.nxFilter);
			writer.Key("nxAstc");
			writer.Int(n3ds.nxAstc);
			writer.Key("nxDocked");
			writer.Bool(n3ds.nxDocked);
			writer.Key("nxFrameGeneration");
			writer.Bool(n3ds.nxFrameGeneration);
			writer.Key("nxSpeedLimit");
			writer.Int(n3ds.nxSpeedLimit);
			writer.Key("nxCovers");
			writer.Int(n3ds.nxCovers);
			writer.Key("nxAntiAliasing");
			writer.Int(n3ds.nxAntiAliasing);
			writer.Key("nxSharpening");
			writer.Int(n3ds.nxSharpening);
			writer.Key("nxController");
			writer.Int(n3ds.nxController);
			writer.Key("nxDeadzone");
			writer.Int(n3ds.nxDeadzone);
			writer.Key("nxOutput");
			writer.Int(n3ds.nxOutput);
			writer.Key("nxHighRefresh");
			writer.Bool(n3ds.nxHighRefresh);
			writer.Key("nxLanguage");
			writer.Int(n3ds.nxLanguage);
			writer.Key("nxMemory");
			writer.Int(n3ds.nxMemory);
			writer.Key("nxAsyncShaders");
			writer.Bool(n3ds.nxAsyncShaders);
			writer.Key("nxFastGpu");
			writer.Bool(n3ds.nxFastGpu);
			writer.Key("nxUnsafeCpu");
			writer.Bool(n3ds.nxUnsafeCpu);
			writer.Key("nxUnsafeDma");
			writer.Bool(n3ds.nxUnsafeDma);
			writer.Key("nxReactiveFlushing");
			writer.Bool(n3ds.nxReactiveFlushing);
			writer.Key("nxSkipInvalidation");
			writer.Bool(n3ds.nxSkipInvalidation);
			writer.Key("nxBlockList");
			writer.Bool(n3ds.nxBlockList);
			writer.Key("nxCompactMemory");
			writer.Bool(n3ds.nxCompactMemory);
			writer.Key("nxRoomHost");
			writer.String(n3ds.nxRoomHost.c_str());
			writer.Key("nxRoomPort");
			writer.Int(n3ds.nxRoomPort);
			writer.Key("nxRoomName");
			writer.String(n3ds.nxRoomName.c_str());
			writer.Key("nxRoomPassword");
			writer.String(n3ds.nxRoomPassword.c_str());
			writer.Key("nxNickname");
			writer.String(n3ds.nxNickname.c_str());
			writer.Key("articAddress");
			writer.String(n3ds.articAddress.c_str());
			writer.Key("gameCount");
			writer.Int(n3ds.gameCount);
			writer.Key("buttons");
			writer.StartObject();
			for (const auto& [button, input] : n3ds.buttons)
			{
				writer.Key(button.c_str());
				writer.String(input.c_str());
			}
			writer.EndObject();
			WriteGames(writer, n3ds.lastGame, n3ds.recent);
			writer.EndObject();
		}
	}

	Launcher Load()
	{
		Launcher settings;
		std::ifstream file(ps5paths::LauncherSettings());
		if (!file)
			return settings;
		std::stringstream text;
		text << file.rdbuf();
		rapidjson::Document json;
		if (json.Parse(text.str().c_str()).HasParseError() || !json.IsObject())
		{
			// kept beside it, not written over by the next Save
			file.close();
			const std::string broken = ps5paths::LauncherSettings() + ".broken";
			std::lock_guard lock(s_brokenMutex);
			if (std::rename(ps5paths::LauncherSettings().c_str(), broken.c_str()) == 0)
				s_broken = broken;
			return settings;
		}
		ReadString(json, "gamesFolder", settings.gamesFolder);
		settings.gamesFolder = ps5paths::Relocated(settings.gamesFolder); // saved before the data moved to a drive (#52)
		settings.gamesFolder = ps5paths::Moved(settings.gamesFolder); // or before the Wii U had a folder of its own (app/reorganize.h)
		ReadInt(json, "upscaleFilter", settings.upscaleFilter, 0, 4);
		ReadBool(json, "highFrameRate", settings.highFrameRate);
		ReadInt(json, "framePacing", settings.framePacing, 1, 3);
		ReadBool(json, "frameGeneration", settings.frameGeneration);
		ReadBool(json, "reshade", settings.reshade);
		ReadBool(json, "overlay", settings.overlay);
		ReadBool(json, "rumble", settings.rumble);
		ReadBool(json, "lookOnShares", settings.lookOnShares);
		ReadString(json, "turbo", settings.turbo);
		if (json.HasMember("touchpadClick") && json["touchpadClick"].IsArray())
			for (rapidjson::SizeType i = 0; i < json["touchpadClick"].Size() && i < 4; i++)
				if (json["touchpadClick"][i].IsInt())
					settings.touchpadClick[i] = std::clamp(json["touchpadClick"][i].GetInt(), 0, 2);
		ReadInt(json, "wiiuFaceLayout", settings.wiiuFaceLayout, 0, 2);
		ReadBool(json, "accountPerUser", settings.accountPerUser);
		if (json.HasMember("accountsByUser") && json["accountsByUser"].IsObject())
			for (const auto& member : json["accountsByUser"].GetObject())
				if (member.value.IsString())
					settings.accountsByUser[member.name.GetString()] = member.value.GetString();
		ReadBool(json, "pinCpuThreads", settings.pinCpuThreads);
		ReadBool(json, "compileBelowNormal", settings.compileBelowNormal);
		ReadInt(json, "idleSpinUs", settings.idleSpinUs, 0, 2000);
		ReadInt(json, "schedulerSpinUs", settings.schedulerSpinUs, 0, 1000);
		ReadInt(json, "idleEventsUs", settings.idleEventsUs, 0, 1000);
		ReadBool(json, "fastTls", settings.fastTls);
		ReadBool(json, "isolateCores", settings.isolateCores);
		ReadBool(json, "standardBorderColors", settings.standardBorderColors);
		ReadBool(json, "shaderDiskCache", settings.shaderDiskCache);
		ReadBool(json, "accurateSingleStores", settings.accurateSingleStores);
		ReadBool(json, "tscTickCount", settings.tscTickCount);
		ReadBool(json, "tscClocks", settings.tscClocks);
		ReadBool(json, "driverClockTsc", settings.driverClockTsc);
		ReadBool(json, "asyncSubmit", settings.asyncSubmit);
		ReadInt(json, "gpuWaitSleepUs", settings.gpuWaitSleepUs, 0, 10000);
		ReadBool(json, "profile", settings.profile);
		ReadBool(json, "heapThreadCache", settings.heapThreadCache);
		ReadBool(json, "alwaysAskHen", settings.alwaysAskHen);
		ReadString(json, "radvDebug", settings.radvDebug);
		if (json.HasMember("radvEnvironment") && json["radvEnvironment"].IsObject())
			for (const auto& member : json["radvEnvironment"].GetObject())
				if (member.value.IsString())
					settings.radvEnvironment[member.name.GetString()] = member.value.GetString();
		ReadInt(json, "cemuSubmitDraws", settings.cemuSubmitDraws, 0, 5000);
		ReadInt(json, "framesInFlight", settings.framesInFlight, 1, 2);
		ReadInt(json, "volume", settings.volume, 0, 100);
		ReadInt(json, "gameCount", settings.gameCount, -1, 1000000);
		ReadString(json, "launchError", settings.launchError);
		ReadString(json, "side", settings.side);
		ReadGames(json, settings.lastGame, settings.recent);
		if (json.HasMember("n3ds") && json["n3ds"].IsObject())
			ReadN3ds(json["n3ds"], settings.n3ds);
		if (json.HasMember("nds") && json["nds"].IsObject())
			ReadN3ds(json["nds"], settings.nds);
		if (settings.n3ds.gamesFolder == ps5paths::Root() + "/azahar/games")
			settings.n3ds.gamesFolder = ps5paths::Azahar() + "/games";
		if (settings.nds.gamesFolder == ps5paths::Root() + "/melonds/games")
			settings.nds.gamesFolder = ps5paths::MelonDs() + "/games";
		if (json.HasMember("switch") && json["switch"].IsObject())
			ReadN3ds(json["switch"], settings.nx, false);
		const char* retroKeys[] = {"gamecube", "wii", "n64", "nes", "gb", "gbc", "gba"};
		for (size_t i = 0; i < 7; i++)
			if (json.HasMember(retroKeys[i]) && json[retroKeys[i]].IsObject()) ReadN3ds(json[retroKeys[i]], settings.retro[i], false);

		// the Switch's default games folder, saved while Eden's folder was another (/data/ps5cemu/eden,
		// or /data/eden before it went beside the others on the app's drive): where Eden's folder is now
		if (settings.nx.gamesFolder == std::string(ps5paths::kDefaultRoot) + "/eden/roms" ||
			settings.nx.gamesFolder == ecam_layout::Sibling(ps5paths::kDefaultRoot, "eden") + "/roms")
			settings.nx.gamesFolder = ps5paths::Eden() + "/roms";
		if (json.HasMember("games") && json["games"].IsObject())
			for (const auto& game : json["games"].GetObject())
			{
				if (!game.value.IsObject())
					continue;
				const std::string key = game.name.GetString();
				const std::string side = key.substr(0, key.find(':'));
				GameValues values;
				for (const auto& member : game.value.GetObject())
				{
					const std::string name = member.name.GetString();
					const auto& names = GameSettingNames(side);
					if (std::find(names.begin(), names.end(), name) == names.end())
						continue;
					if (member.value.IsInt())
						values[name] = member.value.GetInt();
					else if (member.value.IsBool())
						values[name] = member.value.GetBool() ? 1 : 0;
				}
				if (!values.empty())
					settings.games[key] = std::move(values);
			}
		ReadString(json, "music", settings.music);
		// the setup theme is the launcher's only music now: the shop theme's players get it
		if (settings.music == "shop")
			settings.music = "setup";
		else if (settings.music != "setup")
			settings.music = "off";
		ReadInt(json, "musicVolume", settings.musicVolume, 0, 100);
		ReadBool(json, "boxArt", settings.boxArt);
		ReadBool(json, "asyncShaders", settings.asyncShaders);
		ReadBool(json, "shaderNotices", settings.shaderNotices);
		ReadBool(json, "gamePadSpeaker", settings.gamePadSpeaker);
		ReadBool(json, "remoteScreen", settings.remoteScreen);
		ReadBool(json, "menuSounds", settings.menuSounds);
		if (json.HasMember("ui") && json["ui"].IsObject())
		{
			const rapidjson::Value& ui = json["ui"];
			Ui& out = settings.ui;
			ReadString(ui, "lastSide", out.lastSide);
			ReadString(ui, "language", out.language);
			ReadBool(ui, "largerText", out.largerText);
			ReadBool(ui, "highContrast", out.highContrast);
			ReadBool(ui, "reduceMotion", out.reduceMotion);
			ReadBool(ui, "libraryFilters", out.libraryFilters);
			ReadInt(ui, "holdMs", out.holdMs, 400, 1500);
			ReadBool(ui, "gamePictures", out.gamePictures);
			ReadBool(ui, "setupDone", out.setupDone);
			ReadInt(ui, "sides", out.sides, 1, 2047);
			// sides chosen before the Switch had one (4.0.1 and earlier): it is shown once, as a new side
			bool switchOffered = false;
			ReadBool(ui, "switchOffered", switchOffered);
			if (!switchOffered)
				out.sides |= 8;
			ReadString(ui, "lastVersion", out.lastVersion);
			ReadString(ui, "skipVersion", out.skipVersion);
			ReadInt(ui, "reorganizeAsked", out.reorganizeAsked, 0, 10000000);
			ReadString(ui, "theme", out.theme);
			// each side's filter and sort (3.5.0 kept two of each under names of their own; its sort, A to
			// Z by default, is not taken: the Library opens on the games played last now that it is
			// where a side opens)
			ReadInt(ui, "libraryFilterWiiU", out.libraryFilter[0], 0, 3);
			ReadInt(ui, "libraryFilter3ds", out.libraryFilter[1], 0, 3);
			if (out.libraryFilter[1] == 3)
				out.libraryFilter[1] = 0; // the 4.0.0 beta's DS games, which have a side of their own now
			// filters: the Wii U's four (Graphic packs on), the handhelds' three; sorts: four on every side
			auto readSides = [&](const char* key, int (&values)[11], int wiiuMost, int handheldMost) {
				if (ui.HasMember(key) && ui[key].IsArray())
					for (rapidjson::SizeType i = 0; i < ui[key].Size() && i < 11; i++)
						if (ui[key][i].IsInt())
							values[i] = std::clamp(ui[key][i].GetInt(), 0, i == 0 ? wiiuMost : handheldMost);
			};
			readSides("libraryFilter", out.libraryFilter, 3, 2);
			readSides("librarySort", out.librarySort, 3, 3);
		}
		return settings;
	}

	bool Save(const Launcher& settings)
	{
		rapidjson::StringBuffer buffer;
		Writer writer(buffer);
		writer.StartObject();
		writer.Key("gamesFolder");
		writer.String(settings.gamesFolder.c_str());
		writer.Key("upscaleFilter");
		writer.Int(settings.upscaleFilter);
		writer.Key("highFrameRate");
		writer.Bool(settings.highFrameRate);
		writer.Key("framePacing");
		writer.Int(settings.framePacing);
		writer.Key("frameGeneration");
		writer.Bool(settings.frameGeneration);
		writer.Key("reshade");
		writer.Bool(settings.reshade);
		writer.Key("overlay");
		writer.Bool(settings.overlay);
		writer.Key("rumble");
		writer.Bool(settings.rumble);
		writer.Key("lookOnShares");
		writer.Bool(settings.lookOnShares);
		writer.Key("turbo");
		writer.String(settings.turbo.c_str());
		writer.Key("touchpadClick");
		writer.StartArray();
		for (int click : settings.touchpadClick)
			writer.Int(click);
		writer.EndArray();
		writer.Key("wiiuFaceLayout");
		writer.Int(settings.wiiuFaceLayout);
		writer.Key("accountPerUser");
		writer.Bool(settings.accountPerUser);
		writer.Key("accountsByUser");
		writer.StartObject();
		for (const auto& [user, account] : settings.accountsByUser)
		{
			writer.Key(user.c_str());
			writer.String(account.c_str());
		}
		writer.EndObject();
		writer.Key("pinCpuThreads");
		writer.Bool(settings.pinCpuThreads);
		writer.Key("compileBelowNormal");
		writer.Bool(settings.compileBelowNormal);
		writer.Key("idleSpinUs");
		writer.Int(settings.idleSpinUs);
		writer.Key("schedulerSpinUs");
		writer.Int(settings.schedulerSpinUs);
		writer.Key("idleEventsUs");
		writer.Int(settings.idleEventsUs);
		writer.Key("fastTls");
		writer.Bool(settings.fastTls);
		writer.Key("isolateCores");
		writer.Bool(settings.isolateCores);
		writer.Key("standardBorderColors");
		writer.Bool(settings.standardBorderColors);
		writer.Key("shaderDiskCache");
		writer.Bool(settings.shaderDiskCache);
		writer.Key("accurateSingleStores");
		writer.Bool(settings.accurateSingleStores);
		writer.Key("tscTickCount");
		writer.Bool(settings.tscTickCount);
		writer.Key("tscClocks");
		writer.Bool(settings.tscClocks);
		writer.Key("driverClockTsc");
		writer.Bool(settings.driverClockTsc);
		writer.Key("asyncSubmit");
		writer.Bool(settings.asyncSubmit);
		writer.Key("gpuWaitSleepUs");
		writer.Int(settings.gpuWaitSleepUs);
		writer.Key("profile");
		writer.Bool(settings.profile);
		writer.Key("heapThreadCache");
		writer.Bool(settings.heapThreadCache);
		writer.Key("alwaysAskHen");
		writer.Bool(settings.alwaysAskHen);
		writer.Key("radvDebug");
		writer.String(settings.radvDebug.c_str());
		writer.Key("radvEnvironment");
		writer.StartObject();
		for (const auto& [name, value] : settings.radvEnvironment)
		{
			writer.Key(name.c_str());
			writer.String(value.c_str());
		}
		writer.EndObject();
		writer.Key("cemuSubmitDraws");
		writer.Int(settings.cemuSubmitDraws);
		writer.Key("framesInFlight");
		writer.Int(settings.framesInFlight);
		writer.Key("volume");
		writer.Int(settings.volume);
		writer.Key("gameCount");
		writer.Int(settings.gameCount);
		WriteGames(writer, settings.lastGame, settings.recent);
		writer.Key("n3ds");
		WriteN3ds(writer, settings.n3ds);
		writer.Key("nds");
		WriteN3ds(writer, settings.nds);
		writer.Key("switch");
		WriteN3ds(writer, settings.nx);
		const char* retroKeys[] = {"gamecube", "wii", "n64", "nes", "gb", "gbc", "gba"};
		for (size_t i = 0; i < 7; i++) { writer.Key(retroKeys[i]); WriteN3ds(writer, settings.retro[i]); }
		writer.Key("games");
		writer.StartObject();
		for (const auto& [key, values] : settings.games)
		{
			if (values.empty())
				continue;
			writer.Key(key.c_str());
			writer.StartObject();
			for (const auto& [name, value] : values)
			{
				writer.Key(name.c_str());
				writer.Int(value);
			}
			writer.EndObject();
		}
		writer.EndObject();
		writer.Key("music");
		writer.String(settings.music.c_str());
		writer.Key("musicVolume");
		writer.Int(settings.musicVolume);
		writer.Key("boxArt");
		writer.Bool(settings.boxArt);
		writer.Key("asyncShaders");
		writer.Bool(settings.asyncShaders);
		writer.Key("shaderNotices");
		writer.Bool(settings.shaderNotices);
		writer.Key("gamePadSpeaker");
		writer.Bool(settings.gamePadSpeaker);
		writer.Key("remoteScreen");
		writer.Bool(settings.remoteScreen);
		writer.Key("menuSounds");
		writer.Bool(settings.menuSounds);
		writer.Key("side");
		writer.String(settings.side.c_str());
		writer.Key("launchError");
		writer.String(settings.launchError.c_str());
		writer.Key("ui");
		writer.StartObject();
		writer.Key("lastSide");
		writer.String(settings.ui.lastSide.c_str());
		writer.Key("language");
		writer.String(settings.ui.language.c_str());
		writer.Key("largerText");
		writer.Bool(settings.ui.largerText);
		writer.Key("highContrast");
		writer.Bool(settings.ui.highContrast);
		writer.Key("reduceMotion");
		writer.Bool(settings.ui.reduceMotion);
		writer.Key("holdMs");
		writer.Int(settings.ui.holdMs);
		writer.Key("gamePictures");
		writer.Bool(settings.ui.gamePictures);
		writer.Key("setupDone");
		writer.Bool(settings.ui.setupDone);
		writer.Key("sides");
		writer.Int(settings.ui.sides);
		writer.Key("switchOffered");
		writer.Bool(settings.ui.switchOffered);
		writer.Key("lastVersion");
		writer.String(settings.ui.lastVersion.c_str());
		writer.Key("skipVersion");
		writer.String(settings.ui.skipVersion.c_str());
		writer.Key("reorganizeAsked");
		writer.Int(settings.ui.reorganizeAsked);
		writer.Key("theme");
		writer.String(settings.ui.theme.c_str());
		writer.Key("libraryFilters");
		writer.Bool(settings.ui.libraryFilters);
		writer.Key("libraryFilter");
		writer.StartArray();
		for (int filter : settings.ui.libraryFilter)
			writer.Int(filter);
		writer.EndArray();
		writer.Key("librarySort");
		writer.StartArray();
		for (int sort : settings.ui.librarySort)
			writer.Int(sort);
		writer.EndArray();
		writer.EndObject();
		writer.EndObject();
		const std::string temporary = ps5paths::LauncherSettings() + ".tmp";
		{
			std::ofstream file(temporary, std::ios::trunc);
			if (!file)
				return false;
			file << buffer.GetString() << '\n';
		}
		return std::rename(temporary.c_str(), ps5paths::LauncherSettings().c_str()) == 0;
	}

	std::string TakeBrokenFile()
	{
		std::lock_guard lock(s_brokenMutex);
		return std::exchange(s_broken, {});
	}

	// -- games' own settings ------------------------------------------------------------------------

	namespace
	{
		// A setting a number stands for, and its range: on a side's struct (Launcher for the Wii U,
		// N3ds for the 3DS, the DS and the Switch)
		struct Field
		{
			const char* name;
			int Launcher::* number = nullptr;
			bool Launcher::* flag = nullptr;
			int N3ds::* handheldNumber = nullptr;
			bool N3ds::* handheldFlag = nullptr;
			int low = 0, high = 1;
		};

		const std::vector<Field>& WiiUFields()
		{
			static const std::vector<Field> fields = {
				{"upscaleFilter", &Launcher::upscaleFilter, nullptr, nullptr, nullptr, 0, 4},
				{"highFrameRate", nullptr, &Launcher::highFrameRate},
				{"framePacing", &Launcher::framePacing, nullptr, nullptr, nullptr, 1, 3},
				{"frameGeneration", nullptr, &Launcher::frameGeneration},
				{"reshade", nullptr, &Launcher::reshade},
				{"overlay", nullptr, &Launcher::overlay},
				{"asyncShaders", nullptr, &Launcher::asyncShaders},
				{"volume", &Launcher::volume, nullptr, nullptr, nullptr, 0, 100},
			};
			return fields;
		}

		const std::vector<Field>& HandheldFields()
		{
			static const std::vector<Field> fields = {
				{"resolution", nullptr, nullptr, &N3ds::resolution, nullptr, 1, 10},
				{"textureFilter", nullptr, nullptr, &N3ds::textureFilter, nullptr, 0, 5},
				{"customTextures", nullptr, nullptr, nullptr, &N3ds::customTextures},
				{"asyncShaderCompilation", nullptr, nullptr, nullptr, &N3ds::asyncShaders},
				{"layout", nullptr, nullptr, &N3ds::layout, nullptr, 0, 3},
				{"border", nullptr, nullptr, &N3ds::border, nullptr, 0, 5},
				{"volume", nullptr, nullptr, &N3ds::volume, nullptr, 0, 100},
				{"region", nullptr, nullptr, &N3ds::region, nullptr, -1, 6},
				{"language", nullptr, nullptr, &N3ds::language, nullptr, -1, 11},
				{"cpuClock", nullptr, nullptr, &N3ds::cpuClock, nullptr, 25, 400},
				{"dsFilter", nullptr, nullptr, &N3ds::dsFilter, nullptr, 0, 2},
				{"dsRotation", nullptr, nullptr, &N3ds::dsRotation, nullptr, 0, 3},
				{"screenGap", nullptr, nullptr, nullptr, &N3ds::screenGap},
				{"hiddenTouch", nullptr, nullptr, nullptr, &N3ds::hiddenTouch},
				{"frameGeneration", nullptr, nullptr, nullptr, &N3ds::frameGeneration},
				{"dsJit", nullptr, nullptr, nullptr, &N3ds::dsJit},
				{"dsOwnBios", nullptr, nullptr, nullptr, &N3ds::dsOwnBios},
				{"dsSlot2", nullptr, nullptr, nullptr, &N3ds::dsSlot2},
				{"dsStylus", nullptr, nullptr, nullptr, &N3ds::dsStylus},
				{"dsMicSource", nullptr, nullptr, &N3ds::dsMicSource, nullptr, 0, 2},
				{"dsTouchX", nullptr, nullptr, &N3ds::dsTouchX, nullptr, -20, 20},
				{"dsTouchY", nullptr, nullptr, &N3ds::dsTouchY, nullptr, -20, 20},
				{"dsTouchScale", nullptr, nullptr, &N3ds::dsTouchScale, nullptr, 50, 150},
				{"invariantPosition", nullptr, nullptr, nullptr, &N3ds::invariantPosition},
				{"hwShaders", nullptr, nullptr, nullptr, &N3ds::hwShaders},
				{"anaglyph", nullptr, nullptr, nullptr, &N3ds::anaglyph},
				{"nxResolution", nullptr, nullptr, &N3ds::nxResolution, nullptr, 0, 6},
				{"nxFilter", nullptr, nullptr, &N3ds::nxFilter, nullptr, 0, 3},
				{"nxAstc", nullptr, nullptr, &N3ds::nxAstc, nullptr, 0, 1},
				{"nxDocked", nullptr, nullptr, nullptr, &N3ds::nxDocked},
				{"nxFrameGeneration", nullptr, nullptr, nullptr, &N3ds::nxFrameGeneration},
				{"nxSpeedLimit", nullptr, nullptr, &N3ds::nxSpeedLimit, nullptr, 0, 4},
				{"nxAntiAliasing", nullptr, nullptr, &N3ds::nxAntiAliasing, nullptr, 0, 2},
				{"nxSharpening", nullptr, nullptr, &N3ds::nxSharpening, nullptr, 0, 200},
				{"nxController", nullptr, nullptr, &N3ds::nxController, nullptr, -1, 4},
				{"nxOutput", nullptr, nullptr, &N3ds::nxOutput, nullptr, 0, 2},
				{"nxHighRefresh", nullptr, nullptr, nullptr, &N3ds::nxHighRefresh},
				{"nxLanguage", nullptr, nullptr, &N3ds::nxLanguage, nullptr, -1, 17},
				{"nxMemory", nullptr, nullptr, &N3ds::nxMemory, nullptr, 0, 2},
				{"nxAsyncShaders", nullptr, nullptr, nullptr, &N3ds::nxAsyncShaders},
				{"nxFastGpu", nullptr, nullptr, nullptr, &N3ds::nxFastGpu},
				{"nxUnsafeCpu", nullptr, nullptr, nullptr, &N3ds::nxUnsafeCpu},
				{"nxUnsafeDma", nullptr, nullptr, nullptr, &N3ds::nxUnsafeDma},
				{"nxReactiveFlushing", nullptr, nullptr, nullptr, &N3ds::nxReactiveFlushing},
				{"nxSkipInvalidation", nullptr, nullptr, nullptr, &N3ds::nxSkipInvalidation},
				{"nxBlockList", nullptr, nullptr, nullptr, &N3ds::nxBlockList},
				{"nxCompactMemory", nullptr, nullptr, nullptr, &N3ds::nxCompactMemory},
			};
			return fields;
		}

		const Field* FindField(const std::string& side, const std::string& name)
		{
			for (const Field& field : side == "wiiu" ? WiiUFields() : HandheldFields())
				if (name == field.name)
					return &field;
			return nullptr;
		}

		const N3ds& Handheld(const Launcher& settings, const std::string& side)
		{
			return side == "ds" ? settings.nds : side == "switch" ? settings.nx : settings.n3ds;
		}

		N3ds& Handheld(Launcher& settings, const std::string& side)
		{
			return side == "ds" ? settings.nds : side == "switch" ? settings.nx : settings.n3ds;
		}
	}

	std::string GameKey(const std::string& side, uint64_t titleId)
	{
		return side + ":" + Hex(titleId);
	}

	const std::vector<std::string>& GameSettingNames(const std::string& side)
	{
		static const std::vector<std::string> wiiu = {"upscaleFilter", "highFrameRate", "framePacing", "frameGeneration", "reshade", "overlay", "asyncShaders",
			"volume"};
		static const std::vector<std::string> n3ds = {"resolution", "textureFilter", "customTextures", "asyncShaderCompilation", "invariantPosition", "hwShaders", "layout", "border",
			"volume", "region", "language", "cpuClock", "frameGeneration", "dsRotation", "screenGap", "hiddenTouch", "anaglyph"};
		static const std::vector<std::string> nds = {"dsFilter", "dsRotation", "screenGap", "hiddenTouch", "dsJit", "layout", "border", "volume", "language", "dsOwnBios",
			"dsSlot2", "dsMicSource", "dsTouchX", "dsTouchY", "dsTouchScale", "dsStylus"};
		static const std::vector<std::string> nx = {"nxResolution", "nxFilter", "nxAstc", "nxFrameGeneration", "nxDocked", "nxSpeedLimit", "volume",
			"nxAntiAliasing", "nxSharpening", "nxController", "nxOutput", "nxHighRefresh", "nxLanguage", "nxMemory", "nxAsyncShaders", "nxFastGpu",
			"nxUnsafeCpu", "nxUnsafeDma", "nxReactiveFlushing", "nxSkipInvalidation", "nxBlockList", "nxCompactMemory"};
		static const std::vector<std::string> none;
		return side == "wiiu" ? wiiu : side == "3ds" ? n3ds : side == "ds" ? nds : side == "switch" ? nx : none;
	}

	bool Value(const Launcher& settings, const std::string& side, const std::string& name, int& value)
	{
		const Field* field = FindField(side, name);
		if (!field)
			return false;
		if (field->number)
			value = settings.*(field->number);
		else if (field->flag)
			value = settings.*(field->flag) ? 1 : 0;
		else if (field->handheldNumber)
			value = Handheld(settings, side).*(field->handheldNumber);
		else
			value = Handheld(settings, side).*(field->handheldFlag) ? 1 : 0;
		return true;
	}

	void SetValue(Launcher& settings, const std::string& side, const std::string& name, int value)
	{
		const Field* field = FindField(side, name);
		if (!field)
			return;
		value = std::clamp(value, field->low, field->high);
		if (field->number)
			settings.*(field->number) = value;
		else if (field->flag)
			settings.*(field->flag) = value != 0;
		else if (field->handheldNumber)
			Handheld(settings, side).*(field->handheldNumber) = value;
		else
			Handheld(settings, side).*(field->handheldFlag) = value != 0;
	}

	bool PokemonByDefault(uint64_t titleId)
	{
		// the Pokemon games whose outlined models show moving black dots on the console (#23), their
		// lighting and shadows glitching too (the owner, 2026-10-13: X, Y and Ultra Sun), each released
		// worldwide under one title ID
		static constexpr uint64_t kTitles[] = {
			0x0004000000055D00, 0x0004000000055E00, // X, Y
			0x000400000011C400, 0x000400000011C500, // Omega Ruby, Alpha Sapphire
			0x0004000000164800, 0x0004000000175E00, // Sun, Moon
			0x00040000001B5000, 0x00040000001B5100, // Ultra Sun, Ultra Moon
		};
		return std::find(std::begin(kTitles), std::end(kTitles), titleId) != std::end(kTitles);
	}

	Launcher ForGame(const Launcher& settings, const std::string& side, uint64_t titleId)
	{
		Launcher game = settings;
		const auto found = settings.games.find(GameKey(side, titleId));
		// a Pokemon game draws its vertices on the CPU unless it says otherwise (hwShaders). The Outline fix
		// (invariant positions) is no longer on for them by default: on RADV it is what Citra took out for
		// Pokemon's sake (#7376), and the console showed it did not clear their glitches
		if (side == "3ds" && PokemonByDefault(titleId) && (found == settings.games.end() || !found->second.count("hwShaders")))
			game.n3ds.hwShaders = false;
		if (found != settings.games.end())
			for (const auto& [name, value] : found->second)
				SetValue(game, side, name, value);
		return game;
	}

	void SaveChanges(const std::string& side, uint64_t titleId, const Launcher& before, const Launcher& after)
	{
		Launcher all = Load();
		Launcher merged = all;
		// what the menu changes that no game has its own of: as after has it (the buttons, motion,
		// the deadzone, the performance overlay of a handheld), all else the launcher's as it is
		if (side == "wiiu")
		{
			merged.gamePadSpeaker = after.gamePadSpeaker;
			merged.shaderNotices = after.shaderNotices;
			std::copy(std::begin(after.touchpadClick), std::end(after.touchpadClick), std::begin(merged.touchpadClick));
		}
		else
		{
			N3ds& to = Handheld(merged, side);
			const N3ds& from = Handheld(after, side);
			to.motion = from.motion;
			to.deadzone = from.deadzone;
			to.performance = from.performance;
			to.buttons = from.buttons;
			to.nxDeadzone = from.nxDeadzone;
			// the Switch menu's Vibration is Settings > Controls' own, every side's
			if (side == "switch")
				merged.rumble = after.rumble;
		}
		const std::string key = GameKey(side, titleId);
		const auto own = all.games.find(key);
		const bool hasOwn = own != all.games.end() && !own->second.empty();
		for (const std::string& name : GameSettingNames(side))
		{
			int was = 0, now = 0;
			if (!Value(before, side, name, was) || !Value(after, side, name, now) || was == now)
				continue;
			if (hasOwn)
				merged.games[key][name] = now;
			else
				SetValue(merged, side, name, now);
		}
		// should the app start over from here, it comes back to this side
		merged.side = side;
		Save(merged);
	}

	void SaveRemoteScreen(bool on)
	{
		Launcher all = Load();
		if (all.remoteScreen == on)
			return;
		all.remoteScreen = on;
		Save(all);
	}

	void AddRecent(uint64_t& lastGame, std::vector<uint64_t>& recent, uint64_t titleId)
	{
		lastGame = titleId;
		std::erase(recent, titleId);
		recent.insert(recent.begin(), titleId);
		if (recent.size() > 4)
			recent.resize(4);
	}
}
