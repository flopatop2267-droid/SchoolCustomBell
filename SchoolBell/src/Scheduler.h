// Scheduler.h - fires a callback exactly once each time the local clock enters a new minute.
// The PC clock must show local (Moscow, UTC+3) time, which Windows does by default.
#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <string>
#include <thread>
#include <utility>

// Current local time as a std::tm (portable wrapper around localtime_s / localtime_r).
inline std::tm localNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

class Scheduler {
public:
    // Called with "HH:MM" every time a new minute starts.
    using MinuteFn = std::function<void(const std::string& hhmm)>;

    explicit Scheduler(MinuteFn fn) : onMinute_(std::move(fn)) {}
    ~Scheduler() { stop(); }

    void start() {
        running_ = true;
        worker_ = std::thread([this] { loop(); });
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

private:
    // "YYYY-MM-DD HH:MM" - changes exactly once per minute.
    static std::string minuteKey() {
        std::tm tm = localNow();
        char buf[20];
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
        return buf;
    }

    void loop() {
        // Do not ring for the minute the server was started in.
        std::string lastKey = minuteKey();
        while (running_) {
            std::string key = minuteKey();
            if (key != lastKey) {
                lastKey = key;
                onMinute_(key.substr(11));   // "HH:MM"
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    MinuteFn onMinute_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
