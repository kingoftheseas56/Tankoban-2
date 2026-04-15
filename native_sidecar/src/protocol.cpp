#include "protocol.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <thread>

// ---------------------------------------------------------------------------
// Non-blocking stdout write queue
//
// On Windows, fflush(stdout) to a pipe blocks when the pipe buffer is full
// (host isn't reading fast enough). If the heartbeat or time_update thread
// is blocked in fflush holding the mutex, the main thread can't write acks
// for pings, causing mass timeouts.
//
// Fix: producers push serialized JSON lines into a lock-free-ish queue.
// A single writer thread drains the queue to stdout. fflush may still block
// inside the writer thread, but that only blocks the writer — the main thread
// and all producers return immediately.
// ---------------------------------------------------------------------------

static std::mutex               g_queue_mutex;
static std::condition_variable  g_queue_cv;
static std::deque<std::string>  g_queue;
static std::atomic<bool>        g_writer_stop{false};
static std::thread              g_writer_thread;

static void writer_thread_func() {
    while (true) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(g_queue_mutex);
            g_queue_cv.wait(lock, []() {
                return !g_queue.empty() || g_writer_stop.load();
            });
            if (g_queue.empty() && g_writer_stop.load())
                break;
            if (g_queue.empty())
                continue;
            line = std::move(g_queue.front());
            g_queue.pop_front();
        }
        // This may block on pipe — that's fine, only this thread blocks
        std::fputs(line.c_str(), stdout);
        std::fflush(stdout);
    }
    // Drain remaining items before exit
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    for (const auto& line : g_queue) {
        std::fputs(line.c_str(), stdout);
    }
    std::fflush(stdout);
    g_queue.clear();
}

void start_stdout_writer() {
    g_writer_stop.store(false);
    g_writer_thread = std::thread(writer_thread_func);
}

void stop_stdout_writer() {
    g_writer_stop.store(true);
    g_queue_cv.notify_one();
    if (g_writer_thread.joinable())
        g_writer_thread.join();
}

static void enqueue_line(std::string line) {
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_queue.push_back(std::move(line));
    }
    g_queue_cv.notify_one();
}

// ---------------------------------------------------------------------------
// Protocol implementation
// ---------------------------------------------------------------------------

std::optional<Command> parse_command(const std::string& line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (!j.is_object() || j.value("type", "") != "cmd")
            return std::nullopt;

        Command cmd;
        cmd.type      = j.value("type", "");
        cmd.name      = j.value("name", "");
        cmd.sessionId = j.value("sessionId", "");
        cmd.seq       = j.value("seq", 0);
        cmd.payload   = j.value("payload", nlohmann::json::object());
        return cmd;
    } catch (...) {
        return std::nullopt;
    }
}

void write_event(const std::string& name,
                 const std::string& sessionId,
                 int seqAck,
                 const nlohmann::json& payload) {
    nlohmann::json evt;
    evt["type"] = "evt";
    evt["name"] = name;
    evt["sessionId"] = sessionId;
    if (seqAck >= 0)
        evt["seqAck"] = seqAck;
    if (!payload.empty())
        evt["payload"] = payload;

    enqueue_line(evt.dump(-1, ' ', true) + "\n");
}

void write_ack(int seqAck, const std::string& sessionId) {
    write_event("ack", sessionId, seqAck);
}

void write_error(const std::string& code,
                 const std::string& message,
                 const std::string& sessionId,
                 int seqAck) {
    nlohmann::json p;
    p["code"] = code;
    p["message"] = message;
    write_event("error", sessionId, seqAck, p);
}
