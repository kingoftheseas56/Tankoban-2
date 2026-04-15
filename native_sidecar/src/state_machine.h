#pragma once

#include <atomic>
#include <mutex>
#include <string>

enum class State { INIT, READY, OPEN_PENDING, PLAYING, PAUSED, IDLE };

class StateMachine {
public:
    State state() const { return state_.load(std::memory_order_acquire); }
    void set_state(State s) { state_.store(s, std::memory_order_release); }

    std::string sessionId() const {
        std::lock_guard<std::mutex> lock(session_mutex_);
        return session_id_;
    }

    void setSessionId(const std::string& sid) {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id_ = sid;
    }

    // Commands that bypass session-id guard: ping, shutdown, open
    static bool isSessionFree(const std::string& cmdName) {
        return cmdName == "ping" || cmdName == "shutdown" || cmdName == "open";
    }

private:
    std::atomic<State> state_{State::INIT};
    mutable std::mutex session_mutex_;
    std::string session_id_;
};
