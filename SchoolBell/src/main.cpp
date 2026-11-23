// main.cpp - School bell server: scheduler + audio playback + REST API + static web UI.
//
// Libraries (header-only, place in third_party/):
//   httplib.h          https://github.com/yhirose/cpp-httplib
//   nlohmann/json.hpp  https://github.com/nlohmann/json  (version 3.11 or newer)
//   miniaudio.h        https://github.com/mackron/miniaudio  (used in AudioPlayer.cpp)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <regex>
#include <stdexcept>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"

#include "AudioPlayer.h"
#include "Config.h"
#include "Scheduler.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static const char* kConfigPath = "data/config.json";
static const char* kWebDir     = "web";

static std::mutex  g_mtx;      // protects g_cfg
static Config      g_cfg;
static AudioPlayer g_player;

// ---------------------------------------------------------------- helpers

static void reply(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json; charset=utf-8");
}

// Wraps a route so that any exception becomes an HTTP 400 JSON error.
template <typename F>
static httplib::Server::Handler guard(F f) {
    return [f](const httplib::Request& req, httplib::Response& res) {
        try {
            f(req, res);
        } catch (const std::exception& e) {
            reply(res, json{{"error", e.what()}}, 400);
        }
    };
}

static bool isAudioFile(const fs::path& p) {
    std::string ext = p.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp3" || ext == ".wav";
}

// Sorted list of MP3/WAV file names in a folder (UTF-8).
static std::vector<std::string> listTracks(const std::string& folder) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(fs::u8path(folder), ec), end;
    if (ec) return names;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && isAudioFile(it->path()))
            names.push_back(it->path().filename().u8string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

static bool validHHMM(const std::string& s) {
    static const std::regex re(R"(([01]\d|2[0-3]):[0-5]\d)");
    return std::regex_match(s, re);
}

// Picks the next track according to the play mode. Caller must hold g_mtx.
static std::string chooseTrackLocked() {
    auto names = listTracks(g_cfg.musicFolder);
    if (names.empty()) return "";

    size_t idx;
    if (g_cfg.playMode == "random") {
        static std::mt19937 rng{std::random_device{}()};
        idx = std::uniform_int_distribution<size_t>(0, names.size() - 1)(rng);
    } else {
        idx = static_cast<size_t>(g_cfg.nextTrackIndex) % names.size();
        g_cfg.nextTrackIndex = static_cast<int>((idx + 1) % names.size());
    }
    return (fs::u8path(g_cfg.musicFolder) / names[idx]).u8string();
}

// Plays the next track with the current settings. Used by the scheduler and the test button.
static bool ringBell() {
    std::string track;
    int dur, fade;
    double vol;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        track = chooseTrackLocked();
        dur = g_cfg.durationSec;
        fade = g_cfg.fadeSec;
        vol = g_cfg.volume;
        if (!track.empty()) saveConfig(kConfigPath, g_cfg);   // remember the sequence position
    }
    if (track.empty()) {
        std::cerr << "No MP3/WAV tracks found in the music folder\n";
        return false;
    }
    std::cout << "[bell] " << fs::u8path(track).filename().u8string() << "\n";
    return g_player.play(track, dur, fade, vol);
}

// Called by the scheduler at the start of every minute.
static void onMinute(const std::string& hhmm) {
    bool shouldRing = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        const Profile* p = findProfile(g_cfg, g_cfg.activeProfile);
        if (p)
            for (const auto& b : p->bells)
                if (b.enabled && b.time == hhmm) shouldRing = true;
    }
    if (shouldRing) ringBell();
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    int port = (argc > 1) ? std::stoi(argv[1]) : 8080;

    fs::create_directories("data");
    g_cfg = loadConfig(kConfigPath);
    saveConfig(kConfigPath, g_cfg);   // creates the file with defaults on the first run

    if (!g_player.init()) {
        std::cerr << "Audio device initialisation failed\n";
        return 1;
    }

    Scheduler scheduler(onMinute);
    scheduler.start();

    httplib::Server svr;
    svr.set_payload_max_length(200 * 1024 * 1024);   // allow large MP3 uploads
    svr.set_mount_point("/", kWebDir);               // serves web/index.html, app.js, style.css

    // ---- status (polled by the UI once per second)
    svr.Get("/api/state", guard([](const httplib::Request&, httplib::Response& res) {
        json j;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            j["activeProfile"] = g_cfg.activeProfile;
            j["profiles"] = json::array();
            for (const auto& p : g_cfg.profiles) j["profiles"].push_back(p.name);
            j["trackCount"] = listTracks(g_cfg.musicFolder).size();
        }
        std::string now = g_player.nowPlaying();
        j["playing"] = g_player.isPlaying();
        j["nowPlaying"] = now.empty() ? std::string() : fs::u8path(now).filename().u8string();
        std::tm tm = localNow();
        char buf[9];
        std::strftime(buf, sizeof buf, "%H:%M:%S", &tm);
        j["serverTime"] = buf;
        reply(res, j);
    }));

    // ---- configuration (schedule, profiles, playback settings)
    svr.Get("/api/config", guard([](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mtx);
        reply(res, json(g_cfg));
    }));

    svr.Put("/api/config", guard([](const httplib::Request& req, httplib::Response& res) {
        Config in = json::parse(req.body).get<Config>();

        // Validation: reject nonsense before it reaches the scheduler.
        if (in.durationSec < 1 || in.durationSec > 3600)
            throw std::runtime_error("Duration must be between 1 and 3600 seconds");
        if (in.fadeSec < 0 || in.fadeSec > in.durationSec)
            throw std::runtime_error("Fade must be between 0 and the duration");
        in.volume = std::clamp(in.volume, 0.0, 1.0);
        if (in.playMode != "random") in.playMode = "sequential";
        for (const auto& p : in.profiles)
            for (const auto& b : p.bells)
                if (!validHHMM(b.time))
                    throw std::runtime_error("Invalid bell time: " + b.time);

        std::lock_guard<std::mutex> lk(g_mtx);
        in.nextTrackIndex = g_cfg.nextTrackIndex;   // keep server-side playback position
        if (!findProfile(in, in.activeProfile))
            in.activeProfile = in.profiles.empty() ? std::string() : in.profiles.front().name;
        g_cfg = in;
        saveConfig(kConfigPath, g_cfg);
        reply(res, json{{"ok", true}});
    }));

    // ---- switch the active schedule profile immediately
    svr.Post("/api/profile", guard([](const httplib::Request& req, httplib::Response& res) {
        std::string name = json::parse(req.body).at("name").get<std::string>();
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!findProfile(g_cfg, name)) throw std::runtime_error("Unknown profile: " + name);
        g_cfg.activeProfile = name;
        saveConfig(kConfigPath, g_cfg);
        reply(res, json{{"ok", true}, {"activeProfile", name}});
    }));

    // ---- playlist
    svr.Get("/api/tracks", guard([](const httplib::Request&, httplib::Response& res) {
        std::string folder;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            folder = g_cfg.musicFolder;
        }
        reply(res, json{{"folder", folder}, {"tracks", listTracks(folder)}});
    }));

    // Raw-body upload: POST /api/upload?name=bell1.mp3 with the file bytes as the body.
    svr.Post("/api/upload", guard([](const httplib::Request& req, httplib::Response& res) {
        // Keep only the file name, so an upload can never escape the music folder.
        fs::path name = fs::u8path(req.get_param_value("name")).filename();
        if (name.empty() || !isAudioFile(name))
            throw std::runtime_error("Only .mp3 and .wav files are allowed");

        std::string folder;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            folder = g_cfg.musicFolder;
        }
        fs::create_directories(fs::u8path(folder));
        std::ofstream out(fs::u8path(folder) / name, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot write the file (is it open in another program?)");
        out.write(req.body.data(), static_cast<std::streamsize>(req.body.size()));
        reply(res, json{{"ok", true}, {"file", name.u8string()}});
    }));

    // ---- manual controls
    svr.Post("/api/play", guard([](const httplib::Request&, httplib::Response& res) {
        if (!ringBell()) throw std::runtime_error("No playable tracks in the music folder");
        reply(res, json{{"ok", true}});
    }));

    svr.Post("/api/stop", guard([](const httplib::Request&, httplib::Response& res) {
        g_player.stop();
        reply(res, json{{"ok", true}});
    }));

    // Localhost only by default. Change to "0.0.0.0" to let other PCs on the school LAN open the UI.
    std::cout << "School bell server running: http://localhost:" << port << "\n";
    svr.listen("127.0.0.1", port);

    scheduler.stop();
    return 0;
}
