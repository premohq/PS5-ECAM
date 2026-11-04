// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: RetroAchievements for DS games (achievements.h).

#include "achievements.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"
#include "../ps5/curl_socket.h"
#include "../ps5/net.h"
#include "../ps5/notify.h"

#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rc_client.h>
#include <rc_consoles.h>
#include <rc_hash.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

namespace ps5cheevos
{
	namespace
	{
		std::string AccountFile()
		{
			return ps5paths::Root() + "/retroachievements.json";
		}

		std::mutex s_mutex;		// s_account, s_token
		Account s_account;
		std::string s_token;

		// -- the server: one call at a time, on a thread of the module's own --------------------------

		struct Call
		{
			std::string url, post, type;
			rc_client_server_callback_t callback = nullptr;
			void* data = nullptr;
			uint64_t session = 0; // the client it is for: a call for one that has gone is not answered
		};

		std::mutex s_callMutex;
		std::condition_variable s_wake, s_idle;
		std::deque<Call> s_calls;
		bool s_calling = false;		// a call is being made
		std::thread s_worker;
		std::atomic<uint64_t> s_session{1}; // raised as a client goes

		size_t Collect(char* data, size_t size, size_t count, void* out)
		{
			auto* body = static_cast<std::string*>(out);
			if (body->size() > 8 * 1024 * 1024)
				return 0; // a game's set is a few hundred kilobytes at most
			body->append(data, size * count);
			return size * count;
		}

		// "host:port:address", from the PS5's resolver (ps5/net.h), for the URL's host
		curl_slist* Resolve(const std::string& url)
		{
			const size_t start = url.find("://");
			if (start == std::string::npos)
				return nullptr;
			const size_t end = url.find_first_of(":/", start + 3);
			const std::string host = url.substr(start + 3, end == std::string::npos ? std::string::npos : end - start - 3);
			const bool https = url.compare(0, 8, "https://") == 0;
			return ps5net::AddResolve(nullptr, host, https ? 443 : 80);
		}

		void Perform(const Call& call, std::string& body, long& http)
		{
			CURL* curl = curl_easy_init();
			if (!curl)
				return;
			ps5curl::Setup(curl);
			static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store of its own
			curl_slist* resolve = Resolve(call.url);
			// the connection closed by the server after its answer (ps5/curl_socket.h, #78)
			curl_slist* headers = ps5curl::Headers();
			if (!call.post.empty())
				headers = curl_slist_append(headers, ("Content-Type: " + (call.type.empty() ? std::string("application/x-www-form-urlencoded") : call.type)).c_str());
			curl_easy_setopt(curl, CURLOPT_URL, call.url.c_str());
			if (resolve)
				curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
			// RetroAchievements asks clients to name themselves and rcheevos' version (tools/deps.json's pin)
			static const std::string agent = "PS5-ECAM/" PS5CEMU_VERSION " rcheevos/12.5";
			curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			if (!call.post.empty())
			{
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, call.post.c_str());
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)call.post.size());
			}
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
			const CURLcode result = curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
			if (result != CURLE_OK)
			{
				ps5log::Line("[cheevos] {}: {}", call.url.substr(0, call.url.find('?')), curl_easy_strerror(result));
				http = 0;
			}
			curl_easy_cleanup(curl);
			curl_slist_free_all(headers);
			curl_slist_free_all(resolve);
		}

		void Work()
		{
			for (;;)
			{
				Call call;
				{
					std::unique_lock lock(s_callMutex);
					s_wake.wait(lock, [] { return !s_calls.empty(); });
					call = std::move(s_calls.front());
					s_calls.pop_front();
					s_calling = true;
				}
				std::string body;
				long http = 0;
				Perform(call, body, http);
				// a client that has gone gets no answer (the app starts over after a game anyway)
				if (call.session == s_session.load())
				{
					rc_api_server_response_t response{};
					response.body = body.c_str();
					response.body_length = body.size();
					// no answer at all: one rcheevos tries again (unlocks are kept and sent later)
					response.http_status_code = http ? (int)http : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
					call.callback(&response, call.data);
				}
				{
					std::lock_guard lock(s_callMutex);
					s_calling = false;
				}
				s_idle.notify_all();
			}
		}

		void ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback, void* data, rc_client_t*)
		{
			Call call;
			call.url = request->url ? request->url : "";
			call.post = request->post_data ? request->post_data : "";
			call.type = request->content_type ? request->content_type : "";
			call.callback = callback;
			call.data = data;
			call.session = s_session.load();
			{
				std::lock_guard lock(s_callMutex);
				if (!s_worker.joinable())
				{
					s_worker = std::thread(Work);
					s_worker.detach(); // the app starts over after a game; it waits for nothing at the end
				}
				s_calls.push_back(std::move(call));
			}
			s_wake.notify_one();
		}

		// rcheevos' own messages, warnings and errors only, into the boot log
		void Message(const char* message, const rc_client_t*)
		{
			ps5log::Line("[cheevos] {}", message);
		}

		uint32_t NoMemory(uint32_t, uint8_t*, uint32_t, rc_client_t*)
		{
			return 0;
		}

		void SaveAccount()
		{
			rapidjson::StringBuffer text;
			rapidjson::Writer<rapidjson::StringBuffer> writer(text);
			{
				std::lock_guard lock(s_mutex);
				writer.StartObject();
				writer.Key("user");
				writer.String(s_account.user.c_str());
				writer.Key("token");
				writer.String(s_token.c_str());
				writer.Key("points");
				writer.Uint(s_account.points);
				writer.Key("notices");
				writer.Bool(s_account.notices);
				writer.EndObject();
			}
			std::ofstream file(AccountFile(), std::ios::trunc);
			file << text.GetString() << "\n";
		}

		// -- signing in, from the launcher ---------------------------------------------------------------

		rc_client_t* s_signIn = nullptr; // the client signing in; it goes once it has answered

		void SignedIn(int result, const char* error, rc_client_t* client, void*)
		{
			const rc_client_user_t* user = result == RC_OK ? rc_client_get_user_info(client) : nullptr;
			{
				std::lock_guard lock(s_mutex);
				s_account.busy = false;
				if (user && user->token)
				{
					s_account.signedIn = true;
					s_account.user = user->display_name && *user->display_name ? user->display_name : user->username;
					s_account.points = user->score_softcore;
					s_account.message.clear();
					s_token = user->token;
				}
				else
					// tr: RetroAchievements did not sign in: its own words ({0}) follow
					s_account.message = result == RC_INVALID_CREDENTIALS ? ps5lang::Tr("RetroAchievements did not accept the user name or password.") :
										result == RC_NO_RESPONSE ? ps5lang::Tr("RetroAchievements did not answer. Is the PS5 online?") :
																   ps5lang::TrF("RetroAchievements: {0}", error ? error : "?");
			}
			ps5log::Line("[cheevos] sign-in: {}", user ? fmt::format("{} ({} points)", user->username, user->score_softcore) : std::string(error ? error : "failed"));
			if (user)
				SaveAccount();
		}

		// -- a DS game ---------------------------------------------------------------------------------

		std::mutex s_gameMutex;		// s_client, against the emulation's DoFrame
		rc_client_t* s_client = nullptr;
		std::atomic<bool> s_active{false}; // a client is there: DoFrame has something to do
		ReadMemory s_read = nullptr;
		std::atomic<int> s_loaded{0};	   // 0 loading, 1 a set is loaded, -1 none for this game, -2 it failed
		std::string s_loadError;
		bool s_notices = true;

		uint32_t GameMemory(uint32_t address, uint8_t* buffer, uint32_t size, rc_client_t*)
		{
			return s_read ? s_read(address, buffer, size) : 0;
		}

		void Notify(const std::string& text)
		{
			ps5log::Line("[cheevos] {}", text);
			if (s_notices)
				ps5notify::Send(text);
		}

		// From rc_client_do_frame (the emulation's thread), and from the server's thread for its errors
		void Event(const rc_client_event_t* event, rc_client_t* client)
		{
			switch (event->type)
			{
			case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
				if (event->achievement)
					// tr: a RetroAchievements unlock, as the PS5's notification: its points ({0}) and title ({1})
					Notify(ps5lang::TrP((int)event->achievement->points, "Achievement unlocked: {1} ({0} point)", "Achievement unlocked: {1} ({0} points)",
						std::string(event->achievement->title ? event->achievement->title : "")));
				break;
			case RC_CLIENT_EVENT_GAME_COMPLETED:
			{
				const rc_client_game_t* game = rc_client_get_game_info(client);
				// tr: every RetroAchievements achievement of a game earned ({0}: the game's name)
				Notify(ps5lang::TrF("Every achievement of {0} is yours", game && game->title ? game->title : ""));
				break;
			}
			case RC_CLIENT_EVENT_SERVER_ERROR:
				if (event->server_error)
					ps5log::Line("[cheevos] server: {} ({})", event->server_error->error_message ? event->server_error->error_message : "?",
						event->server_error->api ? event->server_error->api : "?");
				break;
			case RC_CLIENT_EVENT_DISCONNECTED:
				ps5log::Line("[cheevos] an unlock could not be sent yet: it is kept and sent again");
				break;
			case RC_CLIENT_EVENT_RECONNECTED:
				ps5log::Line("[cheevos] the unlocks kept are sent");
				break;
			case RC_CLIENT_EVENT_RESET:
				break; // only on turning hardcore on, which the app never does
			default:
				break;
			}
		}

		void TokenSignIn(int result, const char* error, rc_client_t*, void*)
		{
			if (result != RC_OK)
				ps5log::Line("[cheevos] the saved sign-in did not work: {} ({}); sign in again in Settings > Online", error ? error : "?", result);
		}

		void GameLoaded(int result, const char* error, rc_client_t* client, void*)
		{
			if (result == RC_OK)
			{
				rc_client_user_game_summary_t summary{};
				rc_client_get_user_game_summary(client, &summary);
				const rc_client_game_t* game = rc_client_get_game_info(client);
				s_loaded = summary.num_core_achievements ? 1 : -1;
				ps5log::Line("[cheevos] {}: {} of {} achievements, {} of {} points", game && game->title ? game->title : "the game",
					summary.num_unlocked_achievements, summary.num_core_achievements, summary.points_unlocked, summary.points_core);
				if (summary.num_core_achievements)
					// tr: as a DS game starts: how many of its RetroAchievements are earned ({0} of {1})
					Notify(ps5lang::TrF("RetroAchievements: {0} of {1} achievements", summary.num_unlocked_achievements, summary.num_core_achievements));
				return;
			}
			s_loaded = result == RC_NO_GAME_LOADED ? -1 : -2;
			s_loadError = error ? error : "";
			ps5log::Line("[cheevos] no set loaded: {} ({})", error ? error : "?", result);
		}
	}

	void Load()
	{
		std::ifstream file(AccountFile());
		if (!file)
			return;
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		rapidjson::Document json;
		if (json.Parse(text.c_str()).HasParseError() || !json.IsObject())
			return;
		std::lock_guard lock(s_mutex);
		if (json.HasMember("user") && json["user"].IsString())
			s_account.user = json["user"].GetString();
		if (json.HasMember("token") && json["token"].IsString())
			s_token = json["token"].GetString();
		if (json.HasMember("points") && json["points"].IsUint())
			s_account.points = json["points"].GetUint();
		if (json.HasMember("notices") && json["notices"].IsBool())
			s_account.notices = json["notices"].GetBool();
		s_account.signedIn = !s_account.user.empty() && !s_token.empty();
	}

	Account Get()
	{
		std::lock_guard lock(s_mutex);
		if (s_signIn && !s_account.busy)
		{
			// the sign-in has answered: its client goes, here rather than in its own callback, once the
			// server's thread is out of it
			bool idle = false;
			{
				std::lock_guard calls(s_callMutex);
				idle = s_calls.empty() && !s_calling;
			}
			if (idle)
			{
				rc_client_destroy(s_signIn);
				s_signIn = nullptr;
			}
		}
		return s_account;
	}

	bool BeginSignIn(const std::string& user, const std::string& password)
	{
		std::lock_guard lock(s_mutex);
		if (s_account.busy || user.empty() || password.empty())
			return false;
		if (s_signIn)
			rc_client_destroy(s_signIn);
		s_signIn = rc_client_create(NoMemory, ServerCall);
		if (!s_signIn)
			return false;
		rc_client_enable_logging(s_signIn, RC_CLIENT_LOG_LEVEL_WARN, Message);
		s_account.busy = true;
		s_account.message.clear();
		rc_client_begin_login_with_password(s_signIn, user.c_str(), password.c_str(), SignedIn, nullptr);
		return true;
	}

	void SignOut()
	{
		{
			std::lock_guard lock(s_mutex);
			s_account.signedIn = false;
			s_account.user.clear();
			s_account.points = 0;
			s_account.message.clear();
			s_token.clear();
		}
		std::remove(AccountFile().c_str());
		ps5log::Line("[cheevos] signed out");
	}

	void SetNotices(bool on)
	{
		{
			std::lock_guard lock(s_mutex);
			s_account.notices = on;
		}
		if (Get().signedIn)
			SaveAccount();
	}

	std::string HashDs(const uint8_t* rom, size_t size)
	{
		if (!Get().signedIn || !rom || !size)
			return {};
		char hash[33] = {};
		if (!rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO_DS, rom, size))
		{
			ps5log::Line("[cheevos] the ROM could not be hashed: no set looked for");
			return {};
		}
		ps5log::Line("[cheevos] the ROM's hash: {}", hash);
		return hash;
	}

	bool StartGame(ReadMemory read, const std::string& hash)
	{
		std::string user, token;
		{
			std::lock_guard lock(s_mutex);
			if (!s_account.signedIn || hash.empty())
				return false;
			user = s_account.user;
			token = s_token;
			s_notices = s_account.notices;
		}
		std::lock_guard lock(s_gameMutex);
		if (s_client)
			return true;
		s_read = read;
		s_loaded = 0;
		s_client = rc_client_create(GameMemory, ServerCall);
		if (!s_client)
			return false;
		rc_client_enable_logging(s_client, RC_CLIENT_LOG_LEVEL_WARN, Message);
		rc_client_set_event_handler(s_client, Event);
		rc_client_set_hardcore_enabled(s_client, 0); // softcore: states, cheats and speed stay as they are
		rc_client_begin_login_with_token(s_client, user.c_str(), token.c_str(), TokenSignIn, nullptr);
		// the set's lookup waits for the sign-in
		rc_client_begin_load_game(s_client, hash.c_str(), GameLoaded, nullptr);
		s_active = true;
		ps5log::Line("[cheevos] signed in as {}: looking for the game's set", user);
		return true;
	}

	void DoFrame()
	{
		if (!s_active.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(s_gameMutex);
		if (s_client)
			rc_client_do_frame(s_client);
	}

	void Idle()
	{
		if (!s_active.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(s_gameMutex);
		if (s_client)
			rc_client_idle(s_client);
	}

	void Reset()
	{
		if (!s_active.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(s_gameMutex);
		if (s_client)
			rc_client_reset(s_client);
	}

	bool List(std::vector<Item>& items, std::string& summary)
	{
		items.clear();
		if (!s_active.load())
		{
			summary = Get().signedIn ? std::string() : std::string(ps5lang::Tr("Sign in to RetroAchievements in Settings > Online."));
			return false;
		}
		const int loaded = s_loaded.load();
		if (loaded == 0)
		{
			summary = ps5lang::Tr("Looking for this game's achievements…");
			return false;
		}
		if (loaded == -1)
		{
			summary = ps5lang::Tr("RetroAchievements has no achievements for this game.");
			return false;
		}
		if (loaded == -2)
		{
			summary = ps5lang::Tr("RetroAchievements could not be reached. The game plays on without them.");
			return false;
		}
		std::lock_guard lock(s_gameMutex);
		if (!s_client)
			return false;
		rc_client_user_game_summary_t total{};
		rc_client_get_user_game_summary(s_client, &total);
		// tr: a game's RetroAchievements: earned of all ({0} of {1}), then points earned of all ({2} of {3})
		summary = ps5lang::TrF("{0} of {1} achievements · {2} of {3} points", total.num_unlocked_achievements, total.num_core_achievements,
			total.points_unlocked, total.points_core);
		rc_client_achievement_list_t* list =
			rc_client_create_achievement_list(s_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
		if (!list)
			return false;
		for (uint32_t b = 0; b < list->num_buckets; b++)
			for (uint32_t a = 0; a < list->buckets[b].num_achievements; a++)
			{
				const rc_client_achievement_t* achievement = list->buckets[b].achievements[a];
				Item item;
				item.title = achievement->title ? achievement->title : "";
				item.description = achievement->description ? achievement->description : "";
				item.points = achievement->points;
				item.unlocked = achievement->unlocked != 0;
				item.unlockedAt = item.unlocked ? (int64_t)achievement->unlock_time : 0;
				items.push_back(std::move(item));
			}
		rc_client_destroy_achievement_list(list);
		// the unlocked first, the newest of them first; then the rest, as the set orders them
		std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
			if (a.unlocked != b.unlocked)
				return a.unlocked;
			return a.unlocked && a.unlockedAt > b.unlockedAt;
		});
		return true;
	}

	void EndGame(int maxMs)
	{
		if (!s_active.exchange(false))
			return;
		// the unlocks still being sent: a moment for them, then the session ends whatever came of them
		{
			std::unique_lock lock(s_callMutex);
			s_idle.wait_for(lock, std::chrono::milliseconds(maxMs), [] { return s_calls.empty() && !s_calling; });
		}
		std::lock_guard lock(s_gameMutex);
		s_session++; // what is still on its way is for a client that has gone
		if (s_client)
		{
			rc_client_destroy(s_client);
			s_client = nullptr;
		}
		s_read = nullptr;
	}
}
