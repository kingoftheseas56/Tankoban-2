// tankoctl_scenario — TANKOCTL_TEST_HARNESS Phase 1 engine. See header for the
// verb/format contract. Client-side only: one connection per send, same wire
// format as tankoctl's sendCommand, asserts on the returned JSON.

#include "tankoctl_scenario.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>

namespace tankoctl_scenario {
namespace {

constexpr const char* kSocketName    = "TankobanDevControl";
constexpr int         kConnectTimeoutMs = 1000;
constexpr int         kIoTimeoutMs      = 60000;

QString toWire(QString sub)
{
    return sub.replace('-', '_');
}

// Connect → send one command → read one reply. Returns the tankoctl exit-code
// convention: 0 reply, 1 error-type reply, 2 connect failure, 3 timeout. The
// parsed reply (if any) is written to *reply.
int sendAndCapture(const QString& cmd, const QJsonObject& payload, QJsonObject* reply, QString* errMsg)
{
    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(kSocketName));
    if (!sock.waitForConnected(kConnectTimeoutMs)) {
        if (errMsg) *errMsg = QStringLiteral("cannot connect to %1 — is Tankoban running with --dev-control?")
                                  .arg(QString::fromLatin1(kSocketName));
        return 2;
    }

    QJsonObject req;
    req["cmd"]     = cmd;
    req["seq"]     = 1;
    req["payload"] = payload;
    sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    if (!sock.waitForBytesWritten(kIoTimeoutMs)) {
        if (errMsg) *errMsg = QStringLiteral("write timeout");
        return 3;
    }
    if (!sock.waitForReadyRead(kIoTimeoutMs)) {
        if (errMsg) *errMsg = QStringLiteral("no reply within %1ms").arg(kIoTimeoutMs);
        return 3;
    }

    const QByteArray bytes = sock.readAll();
    const QJsonObject obj = QJsonDocument::fromJson(bytes.trimmed()).object();
    if (reply) *reply = obj;
    return obj.value("type").toString() == QLatin1String("error") ? 1 : 0;
}

// Walk dot-path: objects by key, arrays by integer index. e.g. "data.volumes.0.title".
QJsonValue jsonAtPath(const QJsonObject& root, const QString& dotPath)
{
    QJsonValue cur = QJsonValue(root);
    const QStringList parts = dotPath.split('.', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (cur.isObject()) {
            const QJsonObject o = cur.toObject();
            if (!o.contains(part)) return QJsonValue(QJsonValue::Undefined);
            cur = o.value(part);
        } else if (cur.isArray()) {
            bool ok = false;
            const int idx = part.toInt(&ok);
            const QJsonArray arr = cur.toArray();
            if (!ok || idx < 0 || idx >= arr.size()) return QJsonValue(QJsonValue::Undefined);
            cur = arr.at(idx);
        } else {
            return QJsonValue(QJsonValue::Undefined);
        }
    }
    return cur;
}

QString valueToString(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 15);
    case QJsonValue::String: return v.toString();
    case QJsonValue::Null:   return QStringLiteral("null");
    case QJsonValue::Array:  return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)).trimmed();
    case QJsonValue::Object: return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)).trimmed();
    default:                 return QString();
    }
}

bool isKnownOp(const QString& op)
{
    static const QStringList ops = {
        "==", "!=", ">=", "<=", ">", "<", "contains", "matches", "exists"
    };
    return ops.contains(op);
}

// Evaluate reply.<path> <op> <expected>. *got receives a human rendering of the
// actual value for failure messages.
bool evalAssertion(const QJsonObject& reply, const QString& path, const QString& op,
                   const QString& expected, QString* got)
{
    const QJsonValue actual = jsonAtPath(reply, path);
    if (got) *got = actual.isUndefined() ? QStringLiteral("<undefined>") : valueToString(actual);

    if (op == QLatin1String("exists"))
        return !actual.isUndefined() && !actual.isNull();

    if (actual.isUndefined())
        return false;  // path must resolve for every op other than `exists`

    const QString actualStr = valueToString(actual);

    if (op == QLatin1String("contains")) {
        if (actual.isArray()) {
            const QJsonArray arr = actual.toArray();
            for (const QJsonValue& el : arr)
                if (valueToString(el).contains(expected)) return true;
            return false;
        }
        return actualStr.contains(expected);
    }
    if (op == QLatin1String("matches")) {
        const QRegularExpression re(expected);
        if (!re.isValid()) {
            if (got) *got = QStringLiteral("<invalid regex: %1>").arg(re.errorString());
            return false;
        }
        return re.match(actualStr).hasMatch();
    }

    // Numeric comparison only when the actual JSON value is itself a number —
    // never coerce a numeric-LOOKING string (imdbId "123", a hash, a version
    // "10") into a numeric compare, which would make distinct strings test
    // equal. ==/!= fall back to string compare; ordering ops fail loudly on a
    // non-numeric actual rather than doing a surprise lexical-as-numeric test.
    bool eOk = false;
    const double e = expected.toDouble(&eOk);
    const bool actualNumeric = actual.isDouble();
    const double a = actual.toDouble();

    if (op == QLatin1String("==")) return (actualNumeric && eOk) ? (a == e) : (actualStr == expected);
    if (op == QLatin1String("!=")) return (actualNumeric && eOk) ? (a != e) : (actualStr != expected);
    if (op == QLatin1String(">=")) return actualNumeric && eOk && a >= e;
    if (op == QLatin1String("<=")) return actualNumeric && eOk && a <= e;
    if (op == QLatin1String(">"))  return actualNumeric && eOk && a >  e;
    if (op == QLatin1String("<"))  return actualNumeric && eOk && a <  e;
    return false;
}

// Parse a --payload <json-object> flag out of a positional arg list; on success
// removes the pair and fills *payload. Returns false (and sets *parseErr) only
// when --payload is present but its value is not a valid JSON object.
bool extractPayloadFlag(QStringList* positionals, QJsonObject* payload, QString* parseErr)
{
    const int i = positionals->indexOf(QStringLiteral("--payload"));
    if (i < 0) return true;
    if (i + 1 >= positionals->size()) { if (parseErr) *parseErr = QStringLiteral("--payload requires a JSON object"); return false; }
    const QString raw = positionals->at(i + 1);
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (parseErr) *parseErr = QStringLiteral("--payload is not a valid JSON object: %1").arg(pe.errorString());
        return false;
    }
    *payload = doc.object();
    positionals->removeAt(i + 1);
    positionals->removeAt(i);
    return true;
}

int extractIntFlag(QStringList* positionals, const QString& flag, int dflt)
{
    const int i = positionals->indexOf(flag);
    if (i < 0 || i + 1 >= positionals->size()) return dflt;
    bool ok = false;
    const int v = positionals->at(i + 1).toInt(&ok);
    positionals->removeAt(i + 1);
    positionals->removeAt(i);
    return ok ? v : dflt;
}

bool takeFlag(QStringList* positionals, const QString& flag)
{
    const int i = positionals->indexOf(flag);
    if (i < 0) return false;
    positionals->removeAt(i);
    return true;
}

// Poll <cmd> until the assertion holds or timeout. Returns true if satisfied.
bool pollUntil(const QString& cmd, const QJsonObject& payload, const QString& path,
               const QString& op, const QString& value, int timeoutMs, int intervalMs,
               QString* lastGot, QString* fatalErr)
{
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        QJsonObject reply;
        QString sendErr;
        const int rc = sendAndCapture(cmd, payload, &reply, &sendErr);
        if (rc == 2 || rc == 3) { if (fatalErr) *fatalErr = sendErr; return false; }
        if (evalAssertion(reply, path, op, value, lastGot)) return true;
        // An error-type reply won't satisfy the assertion; record it so a
        // timeout FAIL line shows the error, not just a missing/false path.
        if (rc == 1 && lastGot)
            *lastGot = QStringLiteral("error reply: %1 %2")
                           .arg(reply.value("code").toString(), reply.value("message").toString());
        if (clock.elapsed() >= timeoutMs) return false;
        QThread::msleep(static_cast<unsigned long>(qMax(10, intervalMs)));
    }
}

}  // namespace

int runExpect(const QStringList& args)
{
    QTextStream out(stdout), err(stderr);
    QStringList pos = args.mid(2);  // drop [prog, "expect"]
    QJsonObject payload;
    QString perr;
    if (!extractPayloadFlag(&pos, &payload, &perr)) { err << "expect: " << perr << "\n"; return 64; }

    if (pos.size() < 3) {
        err << "usage: tankoctl expect <cmd> <dot.path> <op> <value> [--payload <json>]\n";
        return 64;
    }
    const QString cmd  = toWire(pos.at(0));
    const QString path = pos.at(1);
    const QString op   = pos.at(2);
    const QString val  = pos.value(3);
    if (!isKnownOp(op)) { err << "expect: unknown op '" << op << "' (== != >= <= > < contains matches exists)\n"; return 64; }
    if (op != QLatin1String("exists") && pos.size() < 4) { err << "expect: op '" << op << "' requires a <value>\n"; return 64; }

    QJsonObject reply;
    QString sendErr;
    const int rc = sendAndCapture(cmd, payload, &reply, &sendErr);
    if (rc == 2 || rc == 3) { err << "ERROR: " << sendErr << "\n"; return rc; }

    QString got;
    const bool ok = evalAssertion(reply, path, op, val, &got);
    if (ok) {
        out << "PASS  " << path << ' ' << op << (op == QLatin1String("exists") ? QString() : QStringLiteral(" ") + val)
            << "  (got " << got << ")\n";
        return 0;
    }
    out << "FAIL  " << path << ' ' << op << (op == QLatin1String("exists") ? QString() : QStringLiteral(" ") + val)
        << "  (got " << got << ")\n";
    return 1;
}

int runWaitFor(const QStringList& args)
{
    QTextStream out(stdout), err(stderr);
    QStringList pos = args.mid(2);  // drop [prog, "wait-for"]
    QJsonObject payload;
    QString perr;
    if (!extractPayloadFlag(&pos, &payload, &perr)) { err << "wait-for: " << perr << "\n"; return 64; }
    const int timeoutMs  = extractIntFlag(&pos, QStringLiteral("--timeout"), 10000);
    const int intervalMs = extractIntFlag(&pos, QStringLiteral("--interval"), 250);

    if (pos.size() < 3) {
        err << "usage: tankoctl wait-for <cmd> <dot.path> <op> <value> [--timeout ms] [--interval ms] [--payload <json>]\n";
        return 64;
    }
    const QString cmd  = toWire(pos.at(0));
    const QString path = pos.at(1);
    const QString op   = pos.at(2);
    const QString val  = pos.value(3);
    if (!isKnownOp(op)) { err << "wait-for: unknown op '" << op << "'\n"; return 64; }
    if (op != QLatin1String("exists") && pos.size() < 4) { err << "wait-for: op '" << op << "' requires a <value>\n"; return 64; }

    QString got, fatal;
    const bool ok = pollUntil(cmd, payload, path, op, val, timeoutMs, intervalMs, &got, &fatal);
    if (!fatal.isEmpty()) { err << "ERROR: " << fatal << "\n"; return 2; }
    if (ok) { out << "PASS  " << path << ' ' << op << ' ' << val << "  (got " << got << ")\n"; return 0; }
    out << "FAIL  timed out after " << timeoutMs << "ms — " << path << ' ' << op << ' ' << val
        << "  (last got " << got << ")\n";
    return 1;
}

int runScenario(const QStringList& args)
{
    QTextStream out(stdout), err(stderr);
    QStringList pos = args.mid(2);  // drop [prog, "run"]
    const bool keepGoing = takeFlag(&pos, QStringLiteral("--keep-going"));
    if (pos.isEmpty()) { err << "usage: tankoctl run <scenario.json> [--keep-going]\n"; return 64; }

    const QString path = pos.at(0);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { err << "run: cannot open " << path << "\n"; return 64; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError) { err << "run: bad JSON in " << path << ": " << pe.errorString() << "\n"; return 64; }

    QJsonArray steps;
    QString name = path;
    if (doc.isArray()) {
        steps = doc.array();
    } else if (doc.isObject()) {
        steps = doc.object().value("steps").toArray();
        name  = doc.object().value("name").toString(path);
    } else {
        err << "run: scenario must be a JSON array of steps or {\"steps\":[...]}\n";
        return 64;
    }
    if (steps.isEmpty()) { err << "run: no steps in " << path << "\n"; return 64; }

    out << "RUN  " << name << "  (" << steps.size() << " steps)\n";
    int passed = 0, failed = 0, idx = 0;

    for (const QJsonValue& sv : steps) {
        ++idx;
        const QJsonObject step = sv.toObject();
        const QString cmd   = toWire(step.value("cmd").toString());
        const QJsonObject payload = step.value("payload").toObject();
        const QString label = step.value("label").toString(cmd.isEmpty() ? QStringLiteral("(no cmd)") : cmd);

        QString failReason;
        bool stepOk = true;

        if (cmd.isEmpty()) {
            stepOk = false;
            failReason = QStringLiteral("step has no \"cmd\"");
        }

        // Optional wait_for gate before the command fires.
        if (stepOk && step.contains("wait_for")) {
            const QJsonObject wf = step.value("wait_for").toObject();
            const QString wCmd = wf.contains("cmd") ? toWire(wf.value("cmd").toString()) : cmd;
            const QJsonObject wPayload = wf.contains("payload") ? wf.value("payload").toObject()
                                       : (wf.contains("cmd") ? QJsonObject() : payload);
            const QString wPath = wf.value("path").toString();
            const QString wOp   = wf.value("op").toString();
            const QString wVal  = wf.value("value").toString();
            const int wTimeout  = wf.value("timeout_ms").toInt(10000);
            const int wInterval = wf.value("interval_ms").toInt(250);
            if (wPath.isEmpty() || !isKnownOp(wOp)) {
                stepOk = false;
                failReason = QStringLiteral("wait_for needs a valid path + op");
            } else {
                QString wGot, wFatal;
                const bool satisfied = pollUntil(wCmd, wPayload, wPath, wOp, wVal, wTimeout, wInterval, &wGot, &wFatal);
                if (!wFatal.isEmpty()) { err << "ERROR: " << wFatal << "\n"; return 2; }
                if (!satisfied) {
                    stepOk = false;
                    failReason = QStringLiteral("wait_for timed out (%1ms): %2 %3 %4 (last %5)")
                                     .arg(wTimeout).arg(wPath, wOp, wVal, wGot);
                }
            }
        }

        // Fire the command.
        QJsonObject reply;
        if (stepOk) {
            QString sendErr;
            const int rc = sendAndCapture(cmd, payload, &reply, &sendErr);
            if (rc == 2 || rc == 3) { err << "ERROR: step " << idx << ": " << sendErr << "\n"; return rc; }

            const QJsonArray expects = step.value("expect").toArray();
            if (expects.isEmpty()) {
                if (rc == 1) {  // command returned an error reply and nothing asserts on it
                    stepOk = false;
                    failReason = QStringLiteral("command error: %1 %2")
                                     .arg(reply.value("code").toString(), reply.value("message").toString());
                }
            } else {
                for (const QJsonValue& ev : expects) {
                    const QJsonObject as = ev.toObject();
                    const QString aPath = as.value("path").toString();
                    const QString aOp   = as.value("op").toString();
                    const QString aVal  = as.value("value").toString();
                    if (aPath.isEmpty() || !isKnownOp(aOp)) {
                        stepOk = false;
                        failReason = QStringLiteral("bad expect (needs path + valid op)");
                        break;
                    }
                    QString got;
                    if (!evalAssertion(reply, aPath, aOp, aVal, &got)) {
                        stepOk = false;
                        failReason = QStringLiteral("%1 %2 %3 (got %4)").arg(aPath, aOp, aVal, got);
                        break;
                    }
                }
            }
        }

        if (stepOk) {
            ++passed;
            out << "  [PASS] step " << idx << ": " << label << "\n";
        } else {
            ++failed;
            out << "  [FAIL] step " << idx << ": " << label << " — " << failReason << "\n";
            if (!keepGoing) { out << "  (stopping at first failure; pass --keep-going to continue)\n"; break; }
        }
    }

    out << "DONE  " << passed << " passed, " << failed << " failed"
        << (failed && !keepGoing ? QStringLiteral(" (stopped early)") : QString()) << "\n";
    return failed ? 1 : 0;
}

void appendRecordedStep(const QString& path, const QString& cmd, const QJsonObject& payload)
{
    QJsonArray arr;
    QFile rf(path);
    if (rf.open(QIODevice::ReadOnly)) {
        const QJsonDocument existing = QJsonDocument::fromJson(rf.readAll());
        if (existing.isArray()) arr = existing.array();
        rf.close();
    }
    QJsonObject step;
    step["cmd"] = cmd;
    if (!payload.isEmpty()) step["payload"] = payload;
    arr.append(step);

    if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        rf.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        rf.close();
    } else {
        QTextStream(stderr) << "record: cannot write " << path << "\n";
    }
}

void printUsage(QTextStream& err)
{
    err << "\n  --- TANKOCTL_TEST_HARNESS P1 (test-harness verbs, client-side) ---\n"
        << "  expect <cmd> <dot.path> <op> <value> [--payload <json>]\n"
        << "                                 send <cmd>, assert reply.<path> <op> <value>; exit 0/1\n"
        << "  wait-for <cmd> <dot.path> <op> <value> [--timeout ms] [--interval ms] [--payload <json>]\n"
        << "                                 poll <cmd> until the assertion holds or timeout\n"
        << "  run <scenario.json> [--keep-going]\n"
        << "                                 run an ordered list of steps (wait/cmd/expect); exit 0/1\n"
        << "  record <file> <subcommand> [args...]\n"
        << "                                 run the wrapped command AND append its wire form to <file>\n"
        << "  ops: == != >= <= > < contains matches exists\n";
}

}  // namespace tankoctl_scenario
