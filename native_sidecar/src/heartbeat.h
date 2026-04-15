#pragma once

#include <atomic>
#include <thread>

class Heartbeat {
public:
    // Start emitting heartbeat events every interval_sec seconds.
    void start(double interval_sec = 2.0);

    // Signal the heartbeat thread to stop and join it.
    void stop();

    ~Heartbeat() { stop(); }

private:
    std::atomic<bool> stop_flag_{false};
    std::thread thread_;
};
