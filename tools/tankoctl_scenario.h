// tankoctl_scenario — TANKOCTL_TEST_HARNESS Phase 1 (2026-05-30, Agent 0).
//
// The assertion + scenario engine for tankoctl. Everything here is CLIENT-SIDE:
// it orchestrates the existing connect→send→reply primitive (one command per
// connection, same wire format as the rest of tankoctl) and asserts on the
// returned JSON. No server changes, no schema bump — the bridge already exposes
// the state; this layer just polls it and decides pass/fail.
//
// Verbs (dispatched from tankoctl.cpp main() before the normal command parser):
//   expect <cmd> <dot.path> <op> <value> [--payload <json>]
//       Send <cmd>, read reply, assert reply.<dot.path> <op> <value>.
//       Exit 0 = pass, 1 = fail, 2 = connect error, 3 = timeout.
//   wait-for <cmd> <dot.path> <op> <value> [--timeout <ms>] [--interval <ms>] [--payload <json>]
//       Poll <cmd> until the assertion holds or the timeout elapses (default
//       timeout 10000ms, interval 250ms). Exit 0 = satisfied, 1 = timed out.
//   run <scenario.json> [--keep-going]
//       Execute an ordered list of steps; each step optionally waits, sends a
//       command, and asserts. Stops at first failed step unless --keep-going.
//       Exit 0 = all steps passed, 1 = one or more failed.
//
// Scenario file = a JSON array of step objects, OR {"name":..,"steps":[..]}.
//   step = {
//     "cmd":      "<wire or kebab command>",        // required
//     "payload":  { ... },                          // optional
//     "label":    "human description",              // optional
//     "wait_for": { "cmd":"..", "payload":{}, "path":"..", "op":"..",
//                   "value":"..", "timeout_ms":10000, "interval_ms":250 }, // optional
//     "expect":   [ { "path":"..", "op":"..", "value":".." }, ... ]        // optional
//   }
//
// Operators (shared by expect / wait-for / scenario expects):
//   ==  !=  >=  <=  >  <  contains  matches  exists
//   - numeric ops parse both sides as double; ==/!= try numeric then string.
//   - contains: substring (string actual) or element match (array actual).
//   - matches: regex (QRegularExpression) over the actual rendered as string.
//   - exists: path resolves to a non-null, defined value (no <value> needed).
//
// Dot-path walks objects by key and arrays by integer index, e.g.
//   data.volumes.0.title   or   commands   or   data.count
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QTextStream;

namespace tankoctl_scenario {

// args is the full QCoreApplication::arguments() list (args[1] == verb).
int runExpect(const QStringList& args);
int runWaitFor(const QStringList& args);
int runScenario(const QStringList& args);

// Append one captured step to a record file (read-modify-write a JSON array).
// Used by tankoctl.cpp's `record <file> ...` wrapper.
void appendRecordedStep(const QString& path, const QString& cmd, const QJsonObject& payload);

// Usage lines for the P1 verbs (appended to tankoctl's printUsage).
void printUsage(QTextStream& err);

}  // namespace tankoctl_scenario
