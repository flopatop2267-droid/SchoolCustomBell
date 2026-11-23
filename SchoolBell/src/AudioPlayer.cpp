// AudioPlayer.cpp - miniaudio-based playback.
// miniaudio is a single header library: https://github.com/mackron/miniaudio (third_party/miniaudio.h)
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioPlayer.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

using Clock = std::chrono::steady_clock;

struct AudioPlayer::Impl {
    ma_engine engine{};
    ma_sound sound{};
    bool engineReady = false;
    bool soundReady = false;
    bool playing = false;
    bool fading = false;
    std::string current;
    Clock::time_point endAt;
    int fadeMs = 0;

    mutable std::mutex mtx;        // protects everything above
    std::thread worker;
    std::atomic<bool> running{false};
};

AudioPlayer::AudioPlayer() : impl_(std::make_unique<Impl>()) {}

AudioPlayer::~AudioPlayer() {
    impl_->running = false;
    if (impl_->worker.joinable()) impl_->worker.join();
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        releaseLocked();
    }
    if (impl_->engineReady) ma_engine_uninit(&impl_->engine);
}

bool AudioPlayer::init() {
    if (ma_engine_init(nullptr, &impl_->engine) != MA_SUCCESS) return false;
    impl_->engineReady = true;
    impl_->running = true;
    impl_->worker = std::thread(&AudioPlayer::workerLoop, this);
    return true;
}

bool AudioPlayer::play(const std::string& utf8Path, int durationSec, int fadeSec, double volume) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->engineReady) return false;

    releaseLocked();   // a new bell replaces the one still ringing

    std::filesystem::path p = std::filesystem::u8path(utf8Path);
    ma_result r;
#ifdef _WIN32
    // Wide-char API so Cyrillic folder and file names work on Windows.
    r = ma_sound_init_from_file_w(&impl_->engine, p.wstring().c_str(), 0, nullptr, nullptr, &impl_->sound);
#else
    r = ma_sound_init_from_file(&impl_->engine, p.string().c_str(), 0, nullptr, nullptr, &impl_->sound);
#endif
    if (r != MA_SUCCESS) return false;

    impl_->soundReady = true;
    ma_sound_set_volume(&impl_->sound, static_cast<float>(volume));
    ma_sound_start(&impl_->sound);

    impl_->current = utf8Path;
    impl_->fadeMs = fadeSec * 1000;
    impl_->endAt = Clock::now() + std::chrono::seconds(durationSec);
    impl_->fading = false;
    impl_->playing = true;
    return true;
}

void AudioPlayer::stop() {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    releaseLocked();
}

bool AudioPlayer::isPlaying() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->playing;
}

std::string AudioPlayer::nowPlaying() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->playing ? impl_->current : std::string();
}

void AudioPlayer::releaseLocked() {
    if (impl_->soundReady) {
        ma_sound_uninit(&impl_->sound);
        impl_->soundReady = false;
    }
    impl_->playing = false;
    impl_->fading = false;
}

// Checks every 50 ms: starts the fade at (end - fade), stops the sound at the end.
void AudioPlayer::workerLoop() {
    while (impl_->running) {
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            if (impl_->playing) {
                auto now = Clock::now();
                auto fadeStart = impl_->endAt - std::chrono::milliseconds(impl_->fadeMs);

                if (!impl_->fading && now >= fadeStart) {
                    // Ramp from the current volume down to silence over fadeMs.
                    ma_sound_set_fade_in_milliseconds(&impl_->sound, -1.0f, 0.0f,
                                                      static_cast<ma_uint64>(impl_->fadeMs));
                    impl_->fading = true;
                }

                if (now >= impl_->endAt || ma_sound_at_end(&impl_->sound))
                    releaseLocked();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
