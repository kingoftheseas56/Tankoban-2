#include "heartbeat.h"
#include "protocol.h"
#include <chrono>

void Heartbeat::start(double interval_sec) {
    stop_flag_.store(false);
    thread_ = std::thread([this, interval_sec]() {
        auto interval = std::chrono::milliseconds(
            static_cast<int>(interval_sec * 1000));
        while (!stop_flag_.load()) {
            std::this_thread::sleep_for(interval);
            if (!stop_flag_.load())
                write_event("heartbeat");
        }
    });
}

void Heartbeat::stop() {
    stop_flag_.store(true);
    if (thread_.joinable())
        thread_.join();
}
