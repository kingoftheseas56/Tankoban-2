#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

// Start/stop the background stdout writer thread.
// Must be called from main() before/after any write_event calls.
void start_stdout_writer();
void stop_stdout_writer();

struct Command {
    std::string type;
    std::string name;
    std::string sessionId;
    int seq = 0;
    nlohmann::json payload;
};

// Parse one JSON line from stdin into a Command. Returns nullopt on bad input.
std::optional<Command> parse_command(const std::string& line);

// Write a JSON event to stdout (thread-safe, flushes after each write).
// seqAck < 0 means omit the field (unsolicited events like heartbeat).
void write_event(const std::string& name,
                 const std::string& sessionId = "",
                 int seqAck = -1,
                 const nlohmann::json& payload = nlohmann::json::object());

// Convenience: ack for a command.
void write_ack(int seqAck, const std::string& sessionId);

// Convenience: error event.
void write_error(const std::string& code,
                 const std::string& message,
                 const std::string& sessionId,
                 int seqAck = -1);
