#pragma once
// Shared diagnostic logging for A/V sync debugging.
// diag_open() must be called once from main.cpp before use.
// diag_log() writes to both avsync_diag.log and stderr.

void diag_open();
void diag_log(const char* fmt, ...);
