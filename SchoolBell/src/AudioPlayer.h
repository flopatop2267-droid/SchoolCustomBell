// AudioPlayer.h - plays one bell track at a time through the default Windows output device.
#pragma once

#include <memory>
#include <string>

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // Opens the default playback device. Returns false if audio is unavailable.
    bool init();

    // Plays a file (UTF-8 path). After durationSec the track fades out over fadeSec, then stops.
    // A new call replaces the bell that is currently playing.
    bool play(const std::string& utf8Path, int durationSec, int fadeSec, double volume);

    // Immediate stop (emergency stop).
    void stop();

    bool isPlaying() const;
    std::string nowPlaying() const;   // full path of the current track, or ""

private:
    struct Impl;                       // hides miniaudio types from the rest of the program
    std::unique_ptr<Impl> impl_;

    void workerLoop();                 // background thread: fade timing and cleanup
    void releaseLocked();              // stops and frees the current sound (mutex must be held)
};
