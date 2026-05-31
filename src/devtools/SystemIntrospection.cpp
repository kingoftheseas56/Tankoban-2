#include "devtools/SystemIntrospection.h"

#include "core/CoreBridge.h"
#include "core/JsonStore.h"
#include "core/PosterCache.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QPalette>
#include <QRect>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariant>
#include <QWidget>

namespace {

// ── Allowlists + catalogues ─────────────────────────────────────────────────

// settings-set / settings-reset writable-key allowlist. v1.9 ships a
// conservative set — agents who need a wider surface can ask Agent 0 to
// extend this list in a follow-on commission. Outside-allowlist writes
// return WRITE_KEY_NOT_ALLOWED rather than silently no-op'ing so smokes
// can't drift the on-disk QSettings shape.
const QStringList& writableSettingsKeys()
{
    static const QStringList kKeys = {
        QStringLiteral("theme/mode"),
        QStringLiteral("general/density"),
        QStringLiteral("general/theme"),
        QStringLiteral("general/sortOrder"),
        QStringLiteral("library/lastActivePage"),
        QStringLiteral("library/cardDensity"),
        QStringLiteral("player/volume"),
        QStringLiteral("player/preferredAudioLang"),
        QStringLiteral("player/preferredSubLang"),
    };
    return kKeys;
}

// cache-clear / cache-list known layer names. Each entry maps to a best-
// effort clear strategy in handleCache; layers without a public clear API
// (PosterCache, MangaPosterCache — both private QHash with no clear()) are
// reported as "unsupported" per-layer rather than failing the whole call,
// so cache-clear ALL still returns a useful breakdown.
const QStringList& knownCacheLayers()
{
    static const QStringList kLayers = {
        QStringLiteral("poster"),
        QStringLiteral("manga-poster"),
        QStringLiteral("anilist"),
        QStringLiteral("mangaupdates"),
        QStringLiteral("bookwalker"),
        QStringLiteral("edge-tts"),
        QStringLiteral("videos-duration"),
    };
    return kLayers;
}

// log-tail / log-grep component allowlist + on-disk path resolution.
// log-mark writes to ALL FOUR component paths (sidecar/telemetry/events/
// ipc) simultaneously regardless of caller-supplied component — that's
// the whole point of the correlation marker.
struct LogComponent {
    const char* name;
    const char* relPath;
};

const QVector<LogComponent>& logComponents()
{
    static const QVector<LogComponent> kComps = {
        {"sidecar",   "out/sidecar_debug_live.log"},
        {"telemetry", "out/stream_telemetry.log"},
        {"events",    "out/events.jsonl"},
        {"ipc",       "out/ipc_latency.log"},
        {"tankoctl",  "out/tankoctl.log"},
    };
    return kComps;
}

QString resolveLogPath(const QString& component)
{
    for (const auto& c : logComponents()) {
        if (component == QLatin1String(c.name))
            return QString::fromLatin1(c.relPath);
    }
    return {};
}

QStringList logMarkPaths()
{
    return {
        QStringLiteral("out/sidecar_debug_live.log"),
        QStringLiteral("out/stream_telemetry.log"),
        QStringLiteral("out/events.jsonl"),
        QStringLiteral("out/ipc_latency.log"),
    };
}

// ── Reply / error helpers ───────────────────────────────────────────────────

void mergeReply(QJsonObject& reply, const QJsonObject& extra)
{
    // Caller pre-populated "type":"reply" + "seq". Merge succeed-result
    // fields on top; "type" and "seq" stay.
    for (auto it = extra.begin(); it != extra.end(); ++it)
        reply.insert(it.key(), it.value());
}

void setError(QJsonObject& reply, const char* code, const QString& message)
{
    reply["type"]    = QStringLiteral("error");
    reply["code"]    = QString::fromLatin1(code);
    reply["message"] = message;
}

// ── Geometry / object snapshot helpers (shared with UI side) ────────────────

QJsonObject geometryObject(QWidget* w)
{
    if (!w) return {};
    const QRect r = w->geometry();
    return QJsonObject{
        {"x",      r.x()},
        {"y",      r.y()},
        {"width",  r.width()},
        {"height", r.height()},
    };
}

QJsonObject widgetSummary(QWidget* w)
{
    if (!w) return {};
    QJsonObject o;
    o["objectName"] = w->objectName();
    o["className"]  = QString::fromLatin1(w->metaObject()->className());
    o["title"]      = w->windowTitle();
    o["visible"]    = w->isVisible();
    o["enabled"]    = w->isEnabled();
    o["geometry"]   = geometryObject(w);
    return o;
}

// QSettings allKeys() returns the full key set including group-prefixes.
// Convert a flat key set to a nested JSON object so settings-dump can
// return e.g. {"general":{"theme":"nord","density":"comfy"}}.
QJsonObject dumpSettingsGroup(QSettings& s, const QString& group)
{
    QJsonObject out;
    if (group.isEmpty()) {
        const QStringList all = s.allKeys();
        for (const QString& key : all) {
            out[key] = QJsonValue::fromVariant(s.value(key));
        }
        return out;
    }

    const QString prefix = group + QLatin1Char('/');
    const QStringList all = s.allKeys();
    for (const QString& key : all) {
        if (!key.startsWith(prefix)) continue;
        out[key.mid(prefix.length())] = QJsonValue::fromVariant(s.value(key));
    }
    return out;
}

} // namespace

// ── Static catalogue ────────────────────────────────────────────────────────

bool SystemIntrospection::isWriteCapable(const QString& cmd)
{
    static const QSet<QString> kWrites = {
        QStringLiteral("settings_set"),
        QStringLiteral("settings_reset"),
        QStringLiteral("jsonstore_set"),
        QStringLiteral("cache_clear"),
        QStringLiteral("log_set_level"),
        QStringLiteral("theme_reload"),
        QStringLiteral("dev_inject_error"),
        QStringLiteral("dev_toggle_feature"),
    };
    return kWrites.contains(cmd);
}

QStringList SystemIntrospection::commandList()
{
    return {
        // app-* (3)
        QStringLiteral("app_get_active_modals"),
        QStringLiteral("app_get_window_list"),
        QStringLiteral("app_get_shortcut_table"),
        // settings-* (4)
        QStringLiteral("settings_get"),
        QStringLiteral("settings_set"),
        QStringLiteral("settings_dump"),
        QStringLiteral("settings_reset"),
        // jsonstore-* (2)
        QStringLiteral("jsonstore_get"),
        QStringLiteral("jsonstore_set"),
        // cache-* (3)
        QStringLiteral("cache_clear"),
        QStringLiteral("cache_list"),
        QStringLiteral("cache_get_stats"),
        // scanner-* (2)
        QStringLiteral("scanner_get_status"),
        QStringLiteral("scanner_list_watched"),
        // log-* (4)
        QStringLiteral("log_tail"),
        QStringLiteral("log_grep"),
        QStringLiteral("log_mark"),
        QStringLiteral("log_set_level"),
        // events-* (1)
        QStringLiteral("events_tail"),
        // theme-* (3)
        QStringLiteral("theme_get_palette"),
        QStringLiteral("theme_get_applied_stylesheet"),
        QStringLiteral("theme_reload"),
        // font-* (1)
        QStringLiteral("font_list_loaded"),
        // perf-* (3)
        QStringLiteral("perf_mark_start"),
        QStringLiteral("perf_mark_end"),
        QStringLiteral("perf_dump_counters"),
        // dev-* (2)
        QStringLiteral("dev_inject_error"),
        QStringLiteral("dev_toggle_feature"),
    };
}

// ── Construction + top-level dispatch ──────────────────────────────────────

SystemIntrospection::SystemIntrospection(MainWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , m_window(mainWindow)
{
}

bool SystemIntrospection::dispatch(const QString& cmd,
                                   const QJsonObject& payload,
                                   QJsonObject& reply)
{
    if (cmd.startsWith(QLatin1String("app_")))       return handleApp(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("settings_")))  return handleSettings(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("jsonstore_"))) return handleJsonstore(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("cache_")))     return handleCache(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("scanner_")))   return handleScanner(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("log_")))       return handleLog(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("events_")))    return handleEvents(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("theme_")))     return handleTheme(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("font_")))      return handleFont(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("perf_")))      return handlePerf(cmd, payload, reply);
    if (cmd.startsWith(QLatin1String("dev_")))       return handleDev(cmd, payload, reply);
    return false;
}

// ── app-* ───────────────────────────────────────────────────────────────────

bool SystemIntrospection::handleApp(const QString& cmd, const QJsonObject&, QJsonObject& r)
{
    if (cmd == QLatin1String("app_get_active_modals")) {
        QJsonArray modals;
        // Currently active modal (any window blocking input).
        if (QWidget* m = QApplication::activeModalWidget()) {
            modals.append(widgetSummary(m));
        }
        // Visible QDialog instances anywhere in the QObject tree of any
        // top-level widget. The activeModal already-added skip avoids dupes.
        QWidget* activeModal = QApplication::activeModalWidget();
        for (QWidget* tlw : QApplication::topLevelWidgets()) {
            if (!tlw) continue;
            const QList<QDialog*> dialogs = tlw->findChildren<QDialog*>();
            for (QDialog* d : dialogs) {
                if (!d->isVisible()) continue;
                if (d == activeModal) continue;
                modals.append(widgetSummary(d));
            }
        }
        mergeReply(r, QJsonObject{{"modals", modals}});
        return true;
    }

    if (cmd == QLatin1String("app_get_window_list")) {
        QJsonArray windows;
        for (QWidget* tlw : QApplication::topLevelWidgets()) {
            if (!tlw) continue;
            QJsonObject o = widgetSummary(tlw);
            o["isActive"]     = tlw->isActiveWindow();
            o["isMinimized"]  = tlw->isMinimized();
            o["isMaximized"]  = tlw->isMaximized();
            o["isFullScreen"] = tlw->isFullScreen();
            windows.append(o);
        }
        mergeReply(r, QJsonObject{{"windows", windows}});
        return true;
    }

    if (cmd == QLatin1String("app_get_shortcut_table")) {
        QJsonArray shortcuts;
        for (QWidget* tlw : QApplication::topLevelWidgets()) {
            if (!tlw) continue;
            const QList<QShortcut*> shs = tlw->findChildren<QShortcut*>();
            for (QShortcut* s : shs) {
                QJsonObject o;
                o["key"]        = s->key().toString(QKeySequence::PortableText);
                o["whatsThis"]  = s->whatsThis();
                o["enabled"]    = s->isEnabled();
                o["autoRepeat"] = s->autoRepeat();
                QJsonArray keys;
                for (const QKeySequence& ks : s->keys()) {
                    keys.append(ks.toString(QKeySequence::PortableText));
                }
                o["keys"] = keys;
                if (auto* parentWidget = qobject_cast<QWidget*>(s->parent())) {
                    o["ownerObjectName"] = parentWidget->objectName();
                    o["ownerClassName"]  = QString::fromLatin1(parentWidget->metaObject()->className());
                }
                shortcuts.append(o);
            }
        }
        mergeReply(r, QJsonObject{{"shortcuts", shortcuts}});
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("app-* command '%1' not handled").arg(cmd));
    return true;
}

// ── settings-* ──────────────────────────────────────────────────────────────

bool SystemIntrospection::handleSettings(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    QSettings s(QStringLiteral("Tankoban"), QStringLiteral("Tankoban"));

    if (cmd == QLatin1String("settings_get")) {
        const QString key = p.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("settings-get requires 'key' field"));
            return true;
        }
        const QVariant v = s.value(key);
        mergeReply(r, QJsonObject{
            {"key",      key},
            {"value",    QJsonValue::fromVariant(v)},
            {"isNull",   v.isNull()},
            {"typeName", QString::fromLatin1(v.typeName())},
        });
        return true;
    }

    if (cmd == QLatin1String("settings_set")) {
        const QString key = p.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("settings-set requires 'key' field"));
            return true;
        }
        if (!writableSettingsKeys().contains(key)) {
            setError(r, "WRITE_KEY_NOT_ALLOWED",
                QStringLiteral("settings key '%1' not in v1.9 write allowlist; see "
                               "SystemIntrospection::writableSettingsKeys()").arg(key));
            return true;
        }
        const QJsonValue val = p.value(QStringLiteral("value"));
        s.setValue(key, val.toVariant());
        s.sync();
        mergeReply(r, QJsonObject{
            {"key",   key},
            {"value", val},
            {"applied", true},
        });
        return true;
    }

    if (cmd == QLatin1String("settings_dump")) {
        const QString group = p.value(QStringLiteral("group")).toString();
        mergeReply(r, QJsonObject{
            {"group", group},
            {"values", dumpSettingsGroup(s, group)},
        });
        return true;
    }

    if (cmd == QLatin1String("settings_reset")) {
        const QString key = p.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("settings-reset requires 'key' field"));
            return true;
        }
        if (!writableSettingsKeys().contains(key)) {
            setError(r, "WRITE_KEY_NOT_ALLOWED",
                QStringLiteral("settings key '%1' not in v1.9 write allowlist").arg(key));
            return true;
        }
        const bool existed = s.contains(key);
        s.remove(key);
        s.sync();
        mergeReply(r, QJsonObject{
            {"key", key},
            {"existed", existed},
            {"reset", true},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("settings-* command '%1' not handled").arg(cmd));
    return true;
}

// ── jsonstore-* ─────────────────────────────────────────────────────────────

bool SystemIntrospection::handleJsonstore(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    MainWindow* w = m_window.data();
    if (!w || !w->bridge()) {
        setError(r, "NO_BRIDGE", QStringLiteral("CoreBridge unavailable; cannot reach JsonStore"));
        return true;
    }
    JsonStore& store = w->bridge()->store();

    if (cmd == QLatin1String("jsonstore_get")) {
        const QString path = p.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("jsonstore-get requires 'path' field"));
            return true;
        }
        const QJsonObject value = store.read(path);
        mergeReply(r, QJsonObject{
            {"path",    path},
            {"value",   value},
            {"dataDir", store.dataDir()},
        });
        return true;
    }

    if (cmd == QLatin1String("jsonstore_set")) {
        const QString path = p.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("jsonstore-set requires 'path' field"));
            return true;
        }
        const QJsonValue valIn = p.value(QStringLiteral("value"));
        if (!valIn.isObject()) {
            setError(r, "BAD_REQUEST",
                QStringLiteral("jsonstore-set 'value' must be a JSON object (JsonStore "
                               "stores QJsonObject-shaped state per file)"));
            return true;
        }
        store.write(path, valIn.toObject());
        mergeReply(r, QJsonObject{
            {"path",    path},
            {"applied", true},
            {"note",    QStringLiteral("write enqueued; disk commit is async per JsonStore")},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("jsonstore-* command '%1' not handled").arg(cmd));
    return true;
}

// ── cache-* ─────────────────────────────────────────────────────────────────

bool SystemIntrospection::handleCache(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    if (cmd == QLatin1String("cache_list")) {
        QJsonArray layers;
        for (const QString& name : knownCacheLayers()) {
            QJsonObject o;
            o["name"] = name;
            if (name == QLatin1String("poster") || name == QLatin1String("manga-poster")) {
                o["kind"] = QStringLiteral("in-memory pixmap LRU");
                o["clearable"] = false;
                o["note"] = QStringLiteral("private QHash; public size/clear API not yet exposed");
            } else if (name == QLatin1String("anilist")
                    || name == QLatin1String("mangaupdates")
                    || name == QLatin1String("bookwalker")) {
                o["kind"] = QStringLiteral("disk-backed metadata cache");
                o["clearable"] = false;
                o["note"] = QStringLiteral("clear path needs explicit dataDir-relative file list — follow-on");
            } else if (name == QLatin1String("edge-tts")) {
                o["kind"] = QStringLiteral("in-memory voice-line LRU + disk staging");
                o["clearable"] = false;
                o["note"] = QStringLiteral("internal LRU only; no public accessor yet");
            } else if (name == QLatin1String("videos-duration")) {
                o["kind"] = QStringLiteral("disk-backed duration probe cache");
                o["clearable"] = true;
                o["clearPath"] = QStringLiteral("{dataDir}/video_durations.json");
            }
            layers.append(o);
        }
        mergeReply(r, QJsonObject{{"layers", layers}});
        return true;
    }

    if (cmd == QLatin1String("cache_clear")) {
        const QString layer = p.value(QStringLiteral("layer")).toString();
        if (layer.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("cache-clear requires 'layer' field"));
            return true;
        }
        if (!knownCacheLayers().contains(layer)) {
            setError(r, "UNKNOWN_LAYER",
                QStringLiteral("layer '%1' not in cache-list — see cache-list for the catalogue")
                    .arg(layer));
            return true;
        }
        QJsonObject result;
        result["layer"] = layer;
        if (layer == QLatin1String("videos-duration")) {
            // VideosScanner duration cache lives at {dataDir}/video_durations.json.
            // Resolve dataDir via CoreBridge so we don't duplicate the AppData
            // path computation.
            MainWindow* w = m_window.data();
            if (!w || !w->bridge()) {
                result["cleared"] = false;
                result["reason"] = QStringLiteral("CoreBridge unavailable");
            } else {
                const QString path = w->bridge()->dataDir() + QStringLiteral("/video_durations.json");
                const bool removed = QFile::remove(path);
                result["cleared"] = removed;
                result["path"] = path;
                if (!removed) result["reason"] = QStringLiteral("file did not exist or remove failed");
            }
        } else {
            result["cleared"] = false;
            result["reason"] = QStringLiteral("layer lacks public clear API in v1.9 — see cache-list note");
        }
        mergeReply(r, result);
        return true;
    }

    if (cmd == QLatin1String("cache_get_stats")) {
        // v1.11 (TANKOCTL_TEST_HARNESS P2): real poster-cache hit/miss counters,
        // closing one of the v1.9 deferred-12 (cache-list still notes "no public
        // accessor" for the other layers — only the poster LRU is wired here).
        //
        // COUNTING SEMANTIC (honest caveat for test authors): counters are
        // decode-path-INCLUSIVE, not one-per-logical-lookup. PosterCache::
        // decodeFileAsync() does its own internal get() after the caller's get(),
        // so a single cold tile paint records TWO misses (caller miss + decode
        // miss), while a warm repaint records ONE hit. Net: `misses` is inflated
        // relative to `hits` and `hit_rate` skews low. Assert on direction/
        // presence (hit_rate >= threshold, size > 0, evictions == 0), not on an
        // exact misses == tile-count identity. Surfaced in the reply `note` is
        // intentionally omitted to keep the shape stable; see this comment.
        const PosterCache::Stats s = PosterCache::instance().stats();
        const qint64 lookups = s.hits + s.misses;
        const double hitRate = lookups > 0 ? double(s.hits) / double(lookups) : 0.0;
        mergeReply(r, QJsonObject{
            {"layer",     QStringLiteral("poster")},
            {"hits",      double(s.hits)},
            {"misses",    double(s.misses)},
            {"lookups",   double(lookups)},
            {"hit_rate",  hitRate},
            {"evictions", double(s.evictions)},
            {"puts",      double(s.puts)},
            {"size",      s.size},
            {"capacity",  s.capacity},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("cache-* command '%1' not handled").arg(cmd));
    return true;
}

// ── scanner-* ───────────────────────────────────────────────────────────────

bool SystemIntrospection::handleScanner(const QString& cmd, const QJsonObject&, QJsonObject& r)
{
    if (cmd == QLatin1String("scanner_get_status")) {
        // VideosScanner is a one-shot scanner — no live "running/idle" status
        // exposed publicly. What's introspectable is whether the duration
        // cache file exists + its mtime, which agents use as a proxy for
        // "scanner has run at least once". LibraryScanner is similar shape;
        // its "watched" set is the per-domain root folders QSettings list
        // (see scanner-list-watched). BooksScanner was removed 2026-05-27 in
        // the BOOKS_STREMIO_PIVOT §3.8 burn-the-ships backout — Books mode now
        // owns its library via catalogue records, not folder-walker output.
        MainWindow* w = m_window.data();
        QString durationCachePath;
        bool durationCacheExists = false;
        qint64 durationCacheMtime = 0;
        if (w && w->bridge()) {
            durationCachePath = w->bridge()->dataDir() + QStringLiteral("/video_durations.json");
            const QFileInfo fi(durationCachePath);
            durationCacheExists = fi.exists();
            if (durationCacheExists)
                durationCacheMtime = fi.lastModified().toMSecsSinceEpoch();
        }
        mergeReply(r, QJsonObject{
            {"videosScanner",  QJsonObject{
                {"kind",                 QStringLiteral("one-shot")},
                {"durationCachePath",    durationCachePath},
                {"durationCacheExists",  durationCacheExists},
                {"durationCacheMtimeMs", durationCacheMtime},
            }},
            {"note", QStringLiteral("pause/resume/trigger deferred to v1.9.1 follow-on")},
        });
        return true;
    }

    if (cmd == QLatin1String("scanner_list_watched")) {
        // "Watched" = per-domain root folders the scanners walk. Sourced
        // from QSettings via CoreBridge::rootFolders().
        MainWindow* w = m_window.data();
        if (!w || !w->bridge()) {
            setError(r, "NO_BRIDGE", QStringLiteral("CoreBridge unavailable"));
            return true;
        }
        CoreBridge* b = w->bridge();
        const QStringList kDomains = {
            QStringLiteral("videos"),
            QStringLiteral("comics"),
            QStringLiteral("books"),
            QStringLiteral("stream"),
        };
        QJsonObject perDomain;
        for (const QString& d : kDomains) {
            QJsonArray paths;
            for (const QString& p : b->rootFolders(d)) paths.append(p);
            perDomain[d] = paths;
        }
        mergeReply(r, QJsonObject{{"watched", perDomain}});
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("scanner-* command '%1' not handled").arg(cmd));
    return true;
}

// ── log-* + events-* ────────────────────────────────────────────────────────

bool SystemIntrospection::handleLog(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    if (cmd == QLatin1String("log_tail")) {
        const QString component = p.value(QStringLiteral("component")).toString();
        const int n = p.value(QStringLiteral("n")).toInt(50);
        const QString path = resolveLogPath(component);
        if (path.isEmpty()) {
            setError(r, "UNKNOWN_COMPONENT",
                QStringLiteral("component '%1' not in allowlist; "
                               "valid: sidecar, telemetry, events, ipc, tankoctl").arg(component));
            return true;
        }
        QFile f(path);
        if (!f.exists()) {
            mergeReply(r, QJsonObject{
                {"component", component},
                {"path",      path},
                {"exists",    false},
                {"lines",     QJsonArray{}},
            });
            return true;
        }
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            setError(r, "IO_ERROR",
                QStringLiteral("could not open log '%1': %2").arg(path, f.errorString()));
            return true;
        }
        QTextStream in(&f);
        QStringList all;
        while (!in.atEnd()) all.append(in.readLine());
        const int start = qMax(0, all.size() - qMax(1, n));
        QJsonArray lines;
        for (int i = start; i < all.size(); ++i) lines.append(all.at(i));
        mergeReply(r, QJsonObject{
            {"component",  component},
            {"path",       path},
            {"exists",     true},
            {"totalLines", all.size()},
            {"lines",      lines},
        });
        return true;
    }

    if (cmd == QLatin1String("log_grep")) {
        const QString pattern = p.value(QStringLiteral("pattern")).toString();
        if (pattern.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("log-grep requires 'pattern' field"));
            return true;
        }
        const int maxPerFile = p.value(QStringLiteral("maxPerFile")).toInt(50);
        QJsonObject perComponent;
        for (const auto& c : logComponents()) {
            QFile f(QString::fromLatin1(c.relPath));
            if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                perComponent[QString::fromLatin1(c.name)] = QJsonObject{
                    {"exists", f.exists()},
                    {"matches", QJsonArray{}},
                };
                continue;
            }
            QTextStream in(&f);
            QJsonArray matches;
            int lineNo = 0;
            int kept   = 0;
            while (!in.atEnd() && kept < maxPerFile) {
                ++lineNo;
                const QString line = in.readLine();
                if (line.contains(pattern, Qt::CaseInsensitive)) {
                    matches.append(QJsonObject{{"lineNo", lineNo}, {"text", line}});
                    ++kept;
                }
            }
            perComponent[QString::fromLatin1(c.name)] = QJsonObject{
                {"exists",       true},
                {"path",         QString::fromLatin1(c.relPath)},
                {"matches",      matches},
                {"truncatedAt",  kept >= maxPerFile},
            };
        }
        mergeReply(r, QJsonObject{
            {"pattern",      pattern},
            {"perComponent", perComponent},
        });
        return true;
    }

    if (cmd == QLatin1String("log_mark")) {
        // THE headline unlock — write a correlation marker into all four
        // active log streams simultaneously. Format:
        //   \n=== MARK: <label> @ <ISO ts> ===\n
        // Idempotent: repeated calls just append more markers. Missing log
        // files are created (touch + write) so smokes that mark before any
        // log activity still get a discoverable marker.
        const QString label = p.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("log-mark requires 'label' field"));
            return true;
        }
        const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const QByteArray marker = QStringLiteral("\n=== MARK: %1 @ %2 ===\n")
            .arg(label, ts).toUtf8();
        QJsonArray writes;
        for (const QString& path : logMarkPaths()) {
            // mkpath the parent dir so out/ is created if missing.
            const QFileInfo fi(path);
            QDir().mkpath(fi.absolutePath());
            QFile f(path);
            const bool opened = f.open(QIODevice::Append | QIODevice::WriteOnly);
            qint64 written = -1;
            if (opened) {
                written = f.write(marker);
                f.flush();
                f.close();
            }
            writes.append(QJsonObject{
                {"path",    path},
                {"opened",  opened},
                {"written", written},
            });
        }
        mergeReply(r, QJsonObject{
            {"label",  label},
            {"ts",     ts},
            {"marker", QString::fromUtf8(marker).trimmed()},
            {"writes", writes},
        });
        return true;
    }

    if (cmd == QLatin1String("log_set_level")) {
        const QString component = p.value(QStringLiteral("component")).toString();
        const QString level     = p.value(QStringLiteral("level")).toString();
        if (component.isEmpty() || level.isEmpty()) {
            setError(r, "BAD_REQUEST",
                QStringLiteral("log-set-level requires 'component' and 'level' fields"));
            return true;
        }
        m_logLevels[component] = level;
        mergeReply(r, QJsonObject{
            {"component", component},
            {"level",     level},
            {"applied",   true},
            {"note",      QStringLiteral("recorded in v1.9 in-memory map; consumer-side filter "
                                          "wiring follows in v1.9.1")},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("log-* command '%1' not handled").arg(cmd));
    return true;
}

bool SystemIntrospection::handleEvents(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    if (cmd == QLatin1String("events_tail")) {
        const int n = p.value(QStringLiteral("n")).toInt(20);
        const QString path = QStringLiteral("out/events.jsonl");
        QFile f(path);
        if (!f.exists()) {
            mergeReply(r, QJsonObject{
                {"path",   path},
                {"exists", false},
                {"events", QJsonArray{}},
            });
            return true;
        }
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            setError(r, "IO_ERROR",
                QStringLiteral("could not open events log: %1").arg(f.errorString()));
            return true;
        }
        QTextStream in(&f);
        QStringList all;
        while (!in.atEnd()) all.append(in.readLine());
        const int start = qMax(0, all.size() - qMax(1, n));
        QJsonArray events;
        for (int i = start; i < all.size(); ++i) {
            const QString line = all.at(i);
            // Each row is a JSON object on its own line. Parse so callers
            // get structured data, not raw strings. Lines that fail to
            // parse (e.g. mid-write tear) pass through as {raw: "..."}.
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                events.append(doc.object());
            } else {
                events.append(QJsonObject{{"raw", line}});
            }
        }
        mergeReply(r, QJsonObject{
            {"path",       path},
            {"exists",     true},
            {"totalLines", all.size()},
            {"events",     events},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("events-* command '%1' not handled").arg(cmd));
    return true;
}

// ── theme-* + font-* ────────────────────────────────────────────────────────

bool SystemIntrospection::handleTheme(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    if (cmd == QLatin1String("theme_get_palette")) {
        // ThemePalette stores raw CSS-string tokens (hex, rgb(), rgba()) so
        // buildStylesheet's __PLACEHOLDER__ substitution can shove them
        // straight into QSS without round-tripping through QColor. We surface
        // them as-is so callers see the literal mode-config values.
        const Theme::ThemePalette& palette = Theme::current();
        const Theme::Mode mode = Theme::currentMode();
        const Theme::ModeBlobs blobs = Theme::currentBlobs();

        auto colorToHex = [](const QColor& c) {
            return c.name(QColor::HexArgb);
        };

        mergeReply(r, QJsonObject{
            {"mode",        Theme::slugFor(mode)},
            {"bg0",         palette.bg0},
            {"bg1",         palette.bg1},
            {"text",        palette.text},
            {"textDim",     palette.textDim},
            {"muted",       palette.muted},
            {"border",      palette.border},
            {"borderHover", palette.borderHover},
            {"accent",      palette.accent},
            {"accentSoft",  palette.accentSoft},
            {"accentLine",  palette.accentLine},
            {"topbarBg",    palette.topbarBg},
            {"sidebarBg",   palette.sidebarBg},
            {"menuBg",      palette.menuBg},
            {"toastBg",     palette.toastBg},
            {"cardBg",      palette.cardBg},
            {"overlayDim",  palette.overlayDim},
            {"inkRgb",      palette.inkRgb},
            {"blobs",       QJsonObject{
                {"a", colorToHex(blobs.a)},
                {"b", colorToHex(blobs.b)},
                {"c", colorToHex(blobs.c)},
            }},
        });
        return true;
    }

    if (cmd == QLatin1String("theme_get_applied_stylesheet")) {
        const QString name = p.value(QStringLiteral("objectName")).toString();
        QWidget* target = nullptr;
        if (name.isEmpty()) {
            // No name → return QApplication's global stylesheet.
            mergeReply(r, QJsonObject{
                {"target",     QStringLiteral("<QApplication>")},
                {"stylesheet", qApp ? qApp->styleSheet() : QString()},
            });
            return true;
        }
        for (QWidget* tlw : QApplication::topLevelWidgets()) {
            if (!tlw) continue;
            if (tlw->objectName() == name) { target = tlw; break; }
            target = tlw->findChild<QWidget*>(name);
            if (target) break;
        }
        if (!target) {
            setError(r, "WIDGET_NOT_FOUND",
                QStringLiteral("no widget named '%1' under any top-level window").arg(name));
            return true;
        }
        mergeReply(r, QJsonObject{
            {"target",     name},
            {"className",  QString::fromLatin1(target->metaObject()->className())},
            {"stylesheet", target->styleSheet()},
        });
        return true;
    }

    if (cmd == QLatin1String("theme_reload")) {
        if (!qApp) {
            setError(r, "INTERNAL", QStringLiteral("QApplication instance missing"));
            return true;
        }
        Theme::applyThemeFromSettings(*qApp);
        const Theme::Mode mode = Theme::currentMode();
        mergeReply(r, QJsonObject{
            {"mode",    Theme::slugFor(mode)},
            {"applied", true},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("theme-* command '%1' not handled").arg(cmd));
    return true;
}

bool SystemIntrospection::handleFont(const QString& cmd, const QJsonObject&, QJsonObject& r)
{
    if (cmd == QLatin1String("font_list_loaded")) {
        const QStringList families = QFontDatabase::families();
        QJsonArray arr;
        for (const QString& f : families) arr.append(f);
        mergeReply(r, QJsonObject{
            {"count",    families.size()},
            {"families", arr},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("font-* command '%1' not handled").arg(cmd));
    return true;
}

// ── perf-* ──────────────────────────────────────────────────────────────────

bool SystemIntrospection::handlePerf(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    if (cmd == QLatin1String("perf_mark_start")) {
        const QString label = p.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("perf-mark-start requires 'label' field"));
            return true;
        }
        PerfRegion& reg = m_perfRegions[label];
        if (reg.isOpen) {
            setError(r, "ALREADY_OPEN",
                QStringLiteral("perf region '%1' already open; call perf-mark-end first").arg(label));
            return true;
        }
        reg.isOpen = true;
        reg.running.start();
        mergeReply(r, QJsonObject{
            {"label",     label},
            {"opened",    true},
            {"prevCount", reg.count},
        });
        return true;
    }

    if (cmd == QLatin1String("perf_mark_end")) {
        const QString label = p.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("perf-mark-end requires 'label' field"));
            return true;
        }
        auto it = m_perfRegions.find(label);
        if (it == m_perfRegions.end() || !it->isOpen) {
            setError(r, "NOT_OPEN",
                QStringLiteral("perf region '%1' was not opened by perf-mark-start").arg(label));
            return true;
        }
        const qint64 elapsedNs = it->running.nsecsElapsed();
        it->isOpen   = false;
        it->totalNs += elapsedNs;
        it->count   += 1;
        mergeReply(r, QJsonObject{
            {"label",        label},
            {"elapsedNs",    elapsedNs},
            {"elapsedMs",    static_cast<double>(elapsedNs) / 1.0e6},
            {"countTotal",   it->count},
            {"totalNs",      it->totalNs},
        });
        return true;
    }

    if (cmd == QLatin1String("perf_dump_counters")) {
        QJsonArray regions;
        for (auto it = m_perfRegions.constBegin(); it != m_perfRegions.constEnd(); ++it) {
            QJsonObject o;
            o["label"]     = it.key();
            o["isOpen"]    = it->isOpen;
            o["count"]     = it->count;
            o["totalNs"]   = it->totalNs;
            o["totalMs"]   = static_cast<double>(it->totalNs) / 1.0e6;
            o["avgMs"]     = it->count > 0
                ? (static_cast<double>(it->totalNs) / 1.0e6 / it->count)
                : 0.0;
            regions.append(o);
        }
        mergeReply(r, QJsonObject{{"regions", regions}});
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("perf-* command '%1' not handled").arg(cmd));
    return true;
}

// ── dev-* ───────────────────────────────────────────────────────────────────

bool SystemIntrospection::handleDev(const QString& cmd, const QJsonObject& p, QJsonObject& r)
{
    if (cmd == QLatin1String("dev_inject_error")) {
        const QString code = p.value(QStringLiteral("code")).toString();
        if (code.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("dev-inject-error requires 'code' field"));
            return true;
        }
        const QString note = p.value(QStringLiteral("note")).toString();
        m_errorInjects[code] = note;
        mergeReply(r, QJsonObject{
            {"code",    code},
            {"note",    note},
            {"applied", true},
            {"active",  QJsonArray::fromStringList(m_errorInjects.keys())},
        });
        return true;
    }

    if (cmd == QLatin1String("dev_toggle_feature")) {
        const QString flag = p.value(QStringLiteral("flag")).toString();
        if (flag.isEmpty()) {
            setError(r, "BAD_REQUEST", QStringLiteral("dev-toggle-feature requires 'flag' field"));
            return true;
        }
        // If "value" is provided, set explicitly. Otherwise toggle.
        bool newValue;
        if (p.contains(QStringLiteral("value"))) {
            newValue = p.value(QStringLiteral("value")).toBool();
        } else {
            newValue = !m_featureFlags.value(flag, false);
        }
        m_featureFlags[flag] = newValue;
        mergeReply(r, QJsonObject{
            {"flag",     flag},
            {"value",    newValue},
            {"applied",  true},
        });
        return true;
    }

    setError(r, "UNKNOWN_CMD", QStringLiteral("dev-* command '%1' not handled").arg(cmd));
    return true;
}
