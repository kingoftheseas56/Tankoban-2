#pragma once
#include <string>

// Detect encoding of raw bytes. Returns charset name (e.g. "SHIFT_JIS", "UTF-8").
// Returns "UTF-8" if detection fails or input is already UTF-8.
std::string detect_encoding(const char* data, size_t len);

// Detect encoding and transcode to UTF-8.
// If already UTF-8 or detection fails, returns input unchanged.
std::string transcode_to_utf8(const char* data, size_t len);
