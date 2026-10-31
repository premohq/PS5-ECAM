// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Porpoise-X's settings files (porpoise_settings.h)
#include "porpoise_settings.h"
#include "registry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ps5porpoise
{
	namespace
	{
		std::string Trim(std::string s)
		{
			while (!s.empty() && std::isspace((unsigned char)s.back()))
				s.pop_back();
			size_t i = 0;
			while (i < s.size() && std::isspace((unsigned char)s[i]))
				i++;
			return s.substr(i);
		}

		// "key = value", as Porpoise's Settings::load reads it; false for a comment or anything else
		bool Split(const std::string& line, std::string& key, std::string& value)
		{
			const std::string s = Trim(line);
			const size_t equals = s.find('=');
			if (s.empty() || s[0] == '#' || equals == std::string::npos)
				return false;
			key = Trim(s.substr(0, equals));
			value = Trim(s.substr(equals + 1));
			return !key.empty();
		}

		std::vector<std::string> Lines(const std::string& file)
		{
			std::vector<std::string> lines;
			std::ifstream in(file);
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(line);
			}
			return lines;
		}

		// Written beside it, then put in its place, as Porpoise's own open_atomic
		bool Save(const std::string& file, const std::vector<std::string>& lines, std::string& error)
		{
			std::error_code ec;
			fs::create_directories(fs::path(file).parent_path(), ec);
			const std::string temporary = file + ".tmp";
			{
				std::ofstream out(temporary, std::ios::trunc);
				for (const std::string& line : lines)
					out << line << '\n';
				if (!out.flush())
				{
					error = "cannot write " + temporary;
					return false;
				}
			}
			fs::rename(temporary, file, ec);
			if (ec)
			{
				error = "cannot replace " + file + ": " + ec.message();
				fs::remove(temporary, ec);
				return false;
			}
			return true;
		}

		bool IdCharacters(const char* id)
		{
			for (int i = 0; i < 6; i++)
				if (!std::isupper((unsigned char)id[i]) && !std::isdigit((unsigned char)id[i]))
					return false;
			return true;
		}
	}

	std::string Data() { return ps5retro::Find("gamecube")->data; }
	std::string SettingsFile() { return Data() + "/settings.ini"; }
	std::string GameFile(const std::string& gamePath) { return Data() + "/game-settings/" + GameKey(gamePath) + ".ini"; }

	std::string GameKey(const std::string& gamePath)
	{
		char head[0x8006] = {};
		std::ifstream in(gamePath, std::ios::binary);
		in.read(head, sizeof(head));
		const size_t got = (size_t)in.gcount();
		// where each format keeps the disc's own header, its ID first
		size_t at = 0;
		if (got >= 4 && std::memcmp(head, "WBFS", 4) == 0)
			at = 0x200;
		else if (got >= 4 && (std::memcmp(head, "WIA\x01", 4) == 0 || std::memcmp(head, "RVZ\x01", 4) == 0))
			at = 0x58; // the file's head (0x48 bytes), then the disc's: four fields, then its header
		else if (got >= 4 && std::memcmp(head, "CISO", 4) == 0)
			at = 0x8000;
		if (got >= at + 6 && IdCharacters(head + at))
			return std::string(head + at, 6);
		return fs::path(gamePath).filename().string();
	}

	std::map<std::string, std::string> Read(const std::string& file)
	{
		std::map<std::string, std::string> values;
		std::string key, value;
		for (const std::string& line : Lines(file))
			if (Split(line, key, value))
				values[key] = value;
		return values;
	}

	bool Write(const std::string& file, const std::map<std::string, std::string>& values, std::string& error)
	{
		std::vector<std::string> lines = Lines(file);
		if (lines.empty())
			lines.push_back(file.find("/game-settings/") != std::string::npos ? "# This game's own settings (PS5-ECAM, as Porpoise > Details > Game settings)"
																				: "# Porpoise settings (written by PS5-ECAM's Settings)");
		std::map<std::string, std::string> left = values;
		std::string key, value;
		for (std::string& line : lines)
			if (Split(line, key, value))
				if (const auto it = left.find(key); it != left.end())
				{
					line = key + " = " + it->second;
					left.erase(it);
				}
		for (const auto& [k, v] : left)
			lines.push_back(k + " = " + v);
		return Save(file, lines, error);
	}

	bool Remove(const std::string& file, const std::vector<std::string>& keys, std::string& error)
	{
		std::vector<std::string> lines = Lines(file), kept;
		bool any = false;
		std::string key, value;
		for (const std::string& line : lines)
		{
			const bool setting = Split(line, key, value);
			if (setting && std::find(keys.begin(), keys.end(), key) != keys.end())
				continue;
			any |= setting;
			kept.push_back(line);
		}
		std::error_code ec;
		if (!any)
			return fs::remove(file, ec) || !ec;
		return Save(file, kept, error);
	}
}
