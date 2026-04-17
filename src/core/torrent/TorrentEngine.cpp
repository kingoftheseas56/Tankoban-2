#include "TorrentEngine.h"

#ifdef HAS_LIBTORRENT

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QDebug>
#include <QTimer>
#include <QDateTime>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/ip_filter.hpp>

#include <QSettings>
#include <QStringList>
#include <QHostAddress>

#include <chrono>
#include <fstream>

// ── AlertWorker: runs on a dedicated QThread, drains libtorrent alerts ──────
class TorrentEngine::AlertWorker : public QObject
{
    Q_OBJECT
public:
    AlertWorker(TorrentEngine* engine) : m_engine(engine) {}

public slots:
    void run()
    {
        m_running = true;
        m_traceActive = qEnvironmentVariableIsSet("TANKOBAN_ALERT_TRACE");
        if (m_traceActive) {
            m_traceFile.open("alert_trace.log", std::ios::binary | std::ios::trunc);
            if (m_traceFile.is_open()) {
                m_traceFile << "# Mode A alert trace — hash,type,pieceIdx,blockIdx,wallClockMs\n";
                m_traceFile.flush();
            }
        }
        int progressTick = 0;
        while (m_running) {
            auto* alert = m_engine->m_session.wait_for_alert(std::chrono::milliseconds(250));
            if (alert) drainAlerts();
            triggerPeriodicResumeSaves();

            if (++progressTick >= 4) {
                progressTick = 0;
                emitProgressEvents();
                m_engine->checkSeedingRules();
            }
        }
    }

    void requestStop() { m_running = false; }

private:
    TorrentEngine* m_engine;
    bool m_running = false;
    std::chrono::steady_clock::time_point m_lastResumeSave{std::chrono::steady_clock::now()};
    static constexpr int RESUME_SAVE_INTERVAL_S = 30;

    // STREAM diagnostic (Agent 4B — Mode A alert trace; see alert_mask setup
    // comment for scope + removal condition).
    bool m_traceActive = false;
    std::ofstream m_traceFile;

    void writeAlertTrace(const char* type, const QString& hash,
                         int pieceIdx, int blockIdx)
    {
        if (!m_traceFile.is_open()) return;
        auto nowMs = QDateTime::currentMSecsSinceEpoch();
        m_traceFile << hash.left(10).toStdString()
                    << ',' << type
                    << ',' << pieceIdx
                    << ',' << blockIdx
                    << ',' << nowMs
                    << '\n';
        m_traceFile.flush();
    }

    void drainAlerts()
    {
        std::vector<lt::alert*> alerts;
        m_engine->m_session.pop_alerts(&alerts);

        for (auto* a : alerts) {
            if (auto* mra = lt::alert_cast<lt::metadata_received_alert>(a)) {
                auto hash = TorrentEngine::hashToHex(mra->handle);
                QString name;
                qint64 totalSize = 0;
                QJsonArray files;

                auto ti = mra->handle.torrent_file();
                if (ti) {
                    name = QString::fromStdString(ti->name());
                    totalSize = ti->total_size();
                    auto& fs = ti->files();
                    for (int i = 0; i < fs.num_files(); ++i) {
                        QJsonObject f;
                        f["index"] = i;
                        f["name"]  = QString::fromStdString(fs.file_path(i));
                        f["size"]  = static_cast<qint64>(fs.file_size(i));
                        files.append(f);
                    }
                }

                // Update record
                {
                    QMutexLocker lock(&m_engine->m_mutex);
                    if (m_engine->m_records.contains(hash)) {
                        m_engine->m_records[hash].metadataReady = true;
                        m_engine->m_records[hash].name = name;
                    }
                }

                emit m_engine->metadataReady(hash, name, totalSize, files);
            }
            else if (auto* tea = lt::alert_cast<lt::torrent_error_alert>(a)) {
                auto hash = TorrentEngine::hashToHex(tea->handle);
                emit m_engine->torrentError(hash, QString::fromStdString(tea->message()));
            }
            else if (auto* tfa = lt::alert_cast<lt::torrent_finished_alert>(a)) {
                auto hash = TorrentEngine::hashToHex(tfa->handle);
                emit m_engine->torrentFinished(hash);
            }
            else if (auto* srd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                auto hash = TorrentEngine::hashToHex(srd->handle);
                auto path = m_engine->m_cacheDir + "/resume/" + hash + ".fastresume";
                try {
                    auto buf = lt::write_resume_data_buf(srd->params);
                    std::ofstream ofs(path.toStdString(), std::ios::binary | std::ios::trunc);
                    ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
                } catch (const std::exception& e) {
                    qWarning() << "Failed to write resume data for" << hash << ":" << e.what();
                }
            }
            else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                qWarning() << "Resume data save failed:" << a->message().c_str();
            }
            // STREAM diagnostic (Agent 4B — Mode A alert trace; removed when
            // Mode A closes or Phase 2.3 substrate ships).
            else if (m_traceActive) {
                if (auto* pfa = lt::alert_cast<lt::piece_finished_alert>(a)) {
                    writeAlertTrace("piece_finished",
                                    TorrentEngine::hashToHex(pfa->handle),
                                    static_cast<int>(pfa->piece_index), -1);
                }
                else if (auto* bfa = lt::alert_cast<lt::block_finished_alert>(a)) {
                    writeAlertTrace("block_finished",
                                    TorrentEngine::hashToHex(bfa->handle),
                                    static_cast<int>(bfa->piece_index),
                                    static_cast<int>(bfa->block_index));
                }
            }
        }
    }

    void emitProgressEvents()
    {
        QMutexLocker lock(&m_engine->m_mutex);
        for (auto& rec : m_engine->m_records) {
            if (!rec.handle.is_valid()) continue;
            auto st = rec.handle.status();
            if (st.flags & lt::torrent_flags::paused) continue;

            emit m_engine->torrentProgress(
                rec.infoHash, st.progress,
                st.download_rate, st.upload_rate,
                st.num_peers, st.num_seeds
            );
        }
    }

    void triggerPeriodicResumeSaves()
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastResumeSave).count();
        if (elapsed < RESUME_SAVE_INTERVAL_S) return;
        m_lastResumeSave = now;

        // Drafts in metadata-resolution (savePath = resolve_tmp) aren't in
        // TorrentClient's records.json yet — if the user cancels or the app
        // crashes before startDownload(), the .fastresume lingers with no
        // owner. Skipping save_resume_data for drafts closes the leak source.
        const QString resolveTmp = m_engine->m_cacheDir + QStringLiteral("/resolve_tmp");

        QMutexLocker lock(&m_engine->m_mutex);
        for (auto& rec : m_engine->m_records) {
            if (!rec.handle.is_valid()) continue;
            if (rec.savePath == resolveTmp) continue;
            try {
                rec.handle.save_resume_data(lt::torrent_handle::save_info_dict);
            } catch (...) {}
        }
    }
};

// ── TorrentEngine implementation ────────────────────────────────────────────

TorrentEngine::TorrentEngine(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir)
{
}

TorrentEngine::~TorrentEngine()
{
    stop();
}

void TorrentEngine::ensureDirs()
{
    QDir().mkpath(m_cacheDir);
    QDir().mkpath(m_cacheDir + "/resume");
}

void TorrentEngine::applySettings()
{
    lt::settings_pack sp;

    sp.set_str(lt::settings_pack::listen_interfaces,
               "0.0.0.0:6881,[::]:6881");

    sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
               "router.bittorrent.com:6881,"
               "router.utorrent.com:6881,"
               "dht.transmissionbt.com:6881,"
               "dht.libtorrent.org:25401");

    sp.set_bool(lt::settings_pack::enable_dht, true);
    sp.set_bool(lt::settings_pack::enable_lsd, true);
    sp.set_bool(lt::settings_pack::enable_natpmp, true);
    sp.set_bool(lt::settings_pack::enable_upnp, true);

    // STREAM diagnostic (Agent 4B — Mode A alert trace for cold-session
    // 0%-buffering repro per Agent 4 test session at chat.md:2787-2865).
    // Gated by TANKOBAN_ALERT_TRACE=1 env var so the piece/block alert volume
    // only hits when diagnosing. libtorrent 2.0 splits what used to be a
    // single "progress" category into piece_progress (enables
    // piece_finished_alert) + block_progress (enables block_finished_alert) —
    // we need both for the trace handlers at lines 153/158.
    // Remove when Mode A root cause confirmed OR Phase 2.3 substrate ships
    // (which unconditionally expands the mask per the Axis 2 HELP ACK).
    int alertMask = lt::alert_category::status
                  | lt::alert_category::storage
                  | lt::alert_category::error;
    if (qEnvironmentVariableIsSet("TANKOBAN_ALERT_TRACE")) {
        alertMask |= lt::alert_category::piece_progress
                   | lt::alert_category::block_progress;
    }
    sp.set_int(lt::settings_pack::alert_mask, alertMask);

    // STREAM_PLAYBACK_FIX Phase 3 Batch 3.3 — session settings tuned for
    // streaming. Pre-3.3 values admitted "less aggressive than streaming"
    // (original comment). Reference: Stremio streaming_server settings
    // (stremio-core-development/src/types/streaming_server/settings.rs)
    // + libtorrent streaming guidance at https://www.libtorrent.org/streaming.html.
    //
    // Rationale per setting:
    //   connections_limit 200→400: stream head-fetch benefits from more
    //       parallel peers — more candidates for set_piece_deadline() to
    //       pick the fastest one. Stremio defaults to 200 but runs on
    //       typically-headless server hardware; desktop Windows with
    //       libtorrent 2.x handles 400 comfortably.
    //   active_downloads 5→10, active_seeds 5→10, active_limit 10→20:
    //       prevents the stream torrent from being queued behind prior
    //       library-mode downloads when multiple torrents are active.
    //       A streaming app should treat stream adds as priority.
    //   max_queued_disk_bytes 1MB (default) → 32MB: absorbs piece-write
    //       bursts during head-deadline fan-out without back-pressuring
    //       the scheduler. Streaming workloads write bigger bursts.
    //   request_queue_time 3 (default) → 10: how many seconds of
    //       outstanding requests libtorrent maintains per peer. Streaming
    //       benefits from deeper queues so a slow-to-respond peer
    //       doesn't stall the reader frontier.
    //   request_timeout 10 kept: aggressive dropout for unresponsive
    //       peers is good for streaming head-fetch.
    sp.set_int(lt::settings_pack::connections_limit, 400);

    sp.set_int(lt::settings_pack::active_downloads, 10);
    sp.set_int(lt::settings_pack::active_seeds, 10);
    sp.set_int(lt::settings_pack::active_limit, 20);

    sp.set_int(lt::settings_pack::max_queued_disk_bytes, 32 * 1024 * 1024);
    sp.set_int(lt::settings_pack::request_queue_time, 10);

    sp.set_int(lt::settings_pack::request_timeout, 10);
    sp.set_int(lt::settings_pack::peer_timeout, 20);
    sp.set_int(lt::settings_pack::upload_rate_limit, 0); // unlimited — user controls via setGlobalSpeedLimits()

    // Announce to ALL trackers
    sp.set_bool(lt::settings_pack::announce_to_all_trackers, true);
    sp.set_bool(lt::settings_pack::announce_to_all_tiers, true);

    // Peer connection
    sp.set_int(lt::settings_pack::peer_connect_timeout, 7);
    sp.set_int(lt::settings_pack::min_reconnect_time, 10);
    sp.set_int(lt::settings_pack::connection_speed, 50);
    sp.set_int(lt::settings_pack::mixed_mode_algorithm,
               lt::settings_pack::prefer_tcp);

    // Encryption
    sp.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_enabled);
    sp.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_enabled);
    sp.set_int(lt::settings_pack::allowed_enc_level, lt::settings_pack::pe_both);

    m_session.apply_settings(sp);
}

void TorrentEngine::loadDhtState()
{
    auto path = m_cacheDir + "/session.state";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QByteArray data = file.readAll();
    if (data.isEmpty()) return;

    lt::error_code ec;
    lt::bdecode_node node;
    lt::bdecode(data.data(), data.data() + data.size(), node, ec);
    if (ec) {
        qWarning() << "Failed to bdecode DHT state:" << ec.message().c_str();
        return;
    }
    m_session.load_state(node);
    qDebug() << "Loaded DHT state from" << path << "(" << data.size() << "bytes)";
}

void TorrentEngine::saveDhtState()
{
    auto path = m_cacheDir + "/session.state";
    auto tmp  = path + ".tmp";

    lt::entry state;
    m_session.save_state(state);
    std::vector<char> buf;
    lt::bencode(std::back_inserter(buf), state);

    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(buf.data(), static_cast<qint64>(buf.size()));
    file.close();

    QFile::remove(path);
    QFile::rename(tmp, path);
    qDebug() << "Saved DHT state (" << buf.size() << "bytes)";
}

void TorrentEngine::saveAllResumeData()
{
    // Skip drafts (savePath = resolve_tmp) — see triggerPeriodicResumeSaves.
    const std::string resolveTmp =
        (m_cacheDir + QStringLiteral("/resolve_tmp")).toStdString();

    auto torrents = m_session.get_torrents();
    int count = 0;
    for (auto& h : torrents) {
        if (!h.is_valid()) continue;
        if (h.status().save_path == resolveTmp) continue;
        try {
            h.save_resume_data(lt::torrent_handle::save_info_dict);
            ++count;
        } catch (...) {}
    }
    if (count == 0) return;

    int remaining = count;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (remaining > 0 && std::chrono::steady_clock::now() < deadline) {
        auto* alert = m_session.wait_for_alert(std::chrono::milliseconds(250));
        if (!alert) continue;
        std::vector<lt::alert*> alerts;
        m_session.pop_alerts(&alerts);
        for (auto* a : alerts) {
            if (auto* srd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                auto hash = hashToHex(srd->handle);
                auto resumePath = m_cacheDir + "/resume/" + hash + ".fastresume";
                try {
                    auto buf = lt::write_resume_data_buf(srd->params);
                    std::ofstream ofs(resumePath.toStdString(), std::ios::binary | std::ios::trunc);
                    ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
                } catch (...) {}
                --remaining;
            } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                --remaining;
            }
        }
    }
    qDebug() << "Saved resume data for" << (count - remaining) << "torrents";
}

QString TorrentEngine::hashToHex(const lt::torrent_handle& h)
{
    auto raw = h.info_hashes().get_best().to_string();
    QString hex;
    hex.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        hex += QString::asprintf("%02x", c);
    }
    return hex;
}

QString TorrentEngine::stateToString(lt::torrent_status::state_t s, bool paused)
{
    if (paused) return QStringLiteral("paused");
    switch (s) {
    case lt::torrent_status::checking_files:
    case lt::torrent_status::checking_resume_data:
        return QStringLiteral("checking");
    case lt::torrent_status::downloading_metadata:
        return QStringLiteral("metadata");
    case lt::torrent_status::downloading:
        return QStringLiteral("downloading");
    case lt::torrent_status::finished:
    case lt::torrent_status::seeding:
        return QStringLiteral("seeding");
    default:
        return QStringLiteral("unknown");
    }
}

void TorrentEngine::start()
{
    if (m_running) return;
    ensureDirs();
    applySettings();
    loadDhtState();

    // Re-apply persisted ban list (Phase 6.4) so banned peers stay blocked
    // across app restart. `banPeer()` already writes to QSettings on each ban.
    {
        lt::ip_filter filter = m_session.get_ip_filter();
        for (const QString& ipStr : QSettings().value(
                QStringLiteral("tankorent/bannedPeers")).toStringList()) {
            lt::error_code ec;
            auto addr = lt::make_address(ipStr.toStdString(), ec);
            if (ec) continue;
            filter.add_rule(addr, addr, lt::ip_filter::blocked);
        }
        m_session.set_ip_filter(filter);
    }

    m_alertWorker = new AlertWorker(this);
    m_alertWorker->moveToThread(&m_alertThread);
    connect(&m_alertThread, &QThread::started, m_alertWorker, &AlertWorker::run);
    connect(&m_alertThread, &QThread::finished, m_alertWorker, &QObject::deleteLater);
    m_alertThread.start();

    m_running = true;
    qDebug() << "TorrentEngine started, cache:" << m_cacheDir;
}

void TorrentEngine::stop()
{
    if (!m_running) return;
    m_running = false;

    if (m_alertWorker)
        m_alertWorker->requestStop();
    m_alertThread.quit();
    m_alertThread.wait(5000);

    saveAllResumeData();
    saveDhtState();
    m_session.pause();

    qDebug() << "TorrentEngine stopped";
}

// ── Torrent operations ──────────────────────────────────────────────────────

QString TorrentEngine::addMagnet(const QString& magnetUri, const QString& savePath, bool paused)
{
    lt::error_code ec;
    lt::add_torrent_params atp = lt::parse_magnet_uri(magnetUri.toStdString(), ec);
    if (ec) {
        qWarning() << "Invalid magnet URI:" << ec.message().c_str();
        return {};
    }

    atp.save_path = savePath.toStdString();
    if (paused) {
        // Don't set paused flag — torrent needs to be active to resolve metadata
        // (connect to peers, fetch torrent info via DHT/trackers).
        // Instead, we keep it active but let the caller set file priorities to 0
        // after metadata arrives to prevent downloading actual content.
        atp.flags &= ~lt::torrent_flags::paused;
        atp.flags |= lt::torrent_flags::auto_managed;

        // Upload-only mode: allow peer connections for metadata but minimize traffic
        atp.flags |= lt::torrent_flags::upload_mode;
    }

    auto handle = m_session.add_torrent(std::move(atp), ec);
    if (ec) {
        qWarning() << "Failed to add torrent:" << ec.message().c_str();
        return {};
    }

    auto hash = hashToHex(handle);

    QMutexLocker lock(&m_mutex);
    TorrentRecord rec;
    rec.infoHash = hash;
    rec.name     = QString::fromStdString(handle.status().name);
    rec.savePath = savePath;
    rec.handle   = handle;
    m_records.insert(hash, rec);

    return hash;
}

QString TorrentEngine::addFromResume(const QString& resumePath,
                                      const QString& savePath, bool paused)
{
    QFile file(resumePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot read resume file:" << resumePath;
        return {};
    }
    QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) return {};

    lt::error_code ec;
    lt::add_torrent_params atp = lt::read_resume_data(
        lt::span<const char>(data.data(), static_cast<int>(data.size())), ec);
    if (ec) {
        qWarning() << "Failed to parse resume data:" << ec.message().c_str();
        return {};
    }

    // Override save_path with the persisted value from TorrentClient's JSON
    atp.save_path = savePath.toStdString();

    if (paused) {
        atp.flags |= lt::torrent_flags::paused;
        atp.flags &= ~lt::torrent_flags::auto_managed;
    } else {
        atp.flags &= ~lt::torrent_flags::paused;
        atp.flags |= lt::torrent_flags::auto_managed;
    }

    auto handle = m_session.add_torrent(std::move(atp), ec);
    if (ec) {
        qWarning() << "Failed to add torrent from resume:" << ec.message().c_str();
        return {};
    }

    auto hash = hashToHex(handle);

    QMutexLocker lock(&m_mutex);
    TorrentRecord rec;
    rec.infoHash      = hash;
    rec.name          = QString::fromStdString(handle.status().name);
    rec.savePath      = savePath;
    rec.metadataReady = true;
    rec.handle        = handle;
    m_records.insert(hash, rec);

    return hash;
}

void TorrentEngine::setFilePriorities(const QString& infoHash, const QVector<int>& priorities)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    std::vector<lt::download_priority_t> prio;
    prio.reserve(priorities.size());
    for (int p : priorities)
        prio.push_back(static_cast<lt::download_priority_t>(p));

    it->handle.prioritize_files(prio);
}

void TorrentEngine::setSequentialDownload(const QString& infoHash, bool sequential)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    if (sequential)
        it->handle.set_flags(lt::torrent_flags::sequential_download);
    else
        it->handle.unset_flags(lt::torrent_flags::sequential_download);
}

void TorrentEngine::flattenFiles(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    auto ti = it->handle.torrent_file();
    if (!ti) return;

    auto& fs = ti->files();
    if (fs.num_files() <= 1) return;  // single file — nothing to flatten

    // Multi-file torrents have paths like "TorrentName/file.ext"
    // Strip the root folder prefix so files land directly in save_path
    std::string root = ti->name() + "/";

    for (lt::file_index_t i(0); i < lt::file_index_t(fs.num_files()); ++i) {
        std::string path = fs.file_path(i);
        if (path.size() > root.size() && path.substr(0, root.size()) == root) {
            std::string flat = path.substr(root.size());
            it->handle.rename_file(i, flat);
        }
    }
}

void TorrentEngine::startTorrent(const QString& infoHash, const QString& newSavePath)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    // Clear upload_mode that was set during metadata resolution
    it->handle.unset_flags(lt::torrent_flags::upload_mode);

    // Move storage to the user's chosen destination
    if (!newSavePath.isEmpty() && newSavePath != it->savePath) {
        it->handle.move_storage(newSavePath.toStdString());
        it->savePath = newSavePath;
    }

    it->handle.resume();
}

void TorrentEngine::resumeTorrent(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.set_flags(lt::torrent_flags::auto_managed);
    it->handle.resume();
}

void TorrentEngine::pauseTorrent(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.pause();
    it->handle.save_resume_data(lt::torrent_handle::save_info_dict);
}

void TorrentEngine::removeTorrent(const QString& infoHash, bool deleteFiles)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end()) return;

    if (it->handle.is_valid()) {
        lt::remove_flags_t flags{};
        if (deleteFiles)
            flags = lt::session::delete_files;
        m_session.remove_torrent(it->handle, flags);
    }

    m_records.erase(it);

    // Remove resume data file
    QFile::remove(m_cacheDir + "/resume/" + infoHash + ".fastresume");
}

void TorrentEngine::forceStart(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.unset_flags(lt::torrent_flags::auto_managed);
    it->handle.resume();
}

void TorrentEngine::forceRecheck(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.force_recheck();
}

void TorrentEngine::forceReannounce(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.force_reannounce(0, -1);
}

void TorrentEngine::queuePositionUp(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.queue_position_up();
}

void TorrentEngine::queuePositionDown(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.queue_position_down();
}

void TorrentEngine::setQueueLimits(int maxDownloads, int maxUploads, int maxActive)
{
    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::active_downloads, maxDownloads == 0 ? -1 : maxDownloads);
    sp.set_int(lt::settings_pack::active_seeds,     maxUploads   == 0 ? -1 : maxUploads);
    sp.set_int(lt::settings_pack::active_limit,     maxActive    == 0 ? -1 : maxActive);
    m_session.apply_settings(sp);
}

void TorrentEngine::setSpeedLimits(const QString& infoHash, int dlLimitBps, int ulLimitBps)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.set_download_limit(dlLimitBps);
    it->handle.set_upload_limit(ulLimitBps);
}

void TorrentEngine::setGlobalSpeedLimits(int dlLimitBps, int ulLimitBps)
{
    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::download_rate_limit, dlLimitBps);
    sp.set_int(lt::settings_pack::upload_rate_limit, ulLimitBps);
    m_session.apply_settings(sp);
}

void TorrentEngine::setSeedingRules(const QString& infoHash, float ratioLimit, int seedTimeLimitSecs)
{
    QMutexLocker lock(&m_mutex);
    if (ratioLimit <= 0.f && seedTimeLimitSecs <= 0)
        m_seedingRules.remove(infoHash);
    else
        m_seedingRules[infoHash] = {ratioLimit, seedTimeLimitSecs};
}

void TorrentEngine::setGlobalSeedingRules(float ratioLimit, int seedTimeLimitSecs)
{
    QMutexLocker lock(&m_mutex);
    m_globalSeedRule = {ratioLimit, seedTimeLimitSecs};
}

void TorrentEngine::checkSeedingRules()
{
    QMutexLocker lock(&m_mutex);
    for (auto& rec : m_records) {
        if (!rec.handle.is_valid()) continue;
        auto st = rec.handle.status();
        if (st.state != lt::torrent_status::seeding) continue;
        if (st.flags & lt::torrent_flags::paused) continue;

        SeedingRule rule = m_seedingRules.value(rec.infoHash, m_globalSeedRule);
        if (rule.ratioLimit <= 0.f && rule.seedTimeSecs <= 0) continue;

        if (rule.ratioLimit > 0.f && st.total_done > 0) {
            float ratio = static_cast<float>(st.total_upload) / static_cast<float>(st.total_done);
            if (ratio >= rule.ratioLimit) {
                qDebug() << "Seeding rule: pausing" << rec.infoHash << "ratio" << ratio;
                rec.handle.pause();
                continue;
            }
        }

        if (rule.seedTimeSecs > 0 && st.seeding_duration.count() >= rule.seedTimeSecs) {
            qDebug() << "Seeding rule: pausing" << rec.infoHash << "seed time" << st.seeding_duration.count() << "s";
            rec.handle.pause();
        }
    }
}

QList<TorrentStatus> TorrentEngine::allStatuses() const
{
    QList<TorrentStatus> result;
    QMutexLocker lock(&m_mutex);

    for (auto& rec : m_records) {
        TorrentStatus ts;
        ts.infoHash = rec.infoHash;
        ts.name     = rec.name;
        ts.savePath = rec.savePath;

        if (rec.handle.is_valid()) {
            auto st = rec.handle.status();
            bool paused = !!(st.flags & lt::torrent_flags::paused);
            ts.stateString  = stateToString(st.state, paused);
            ts.progress     = st.progress;
            ts.downloadRate = st.download_rate;
            ts.uploadRate   = st.upload_rate;
            ts.numPeers     = st.num_peers;
            ts.numSeeds     = st.num_seeds;
            ts.totalDone    = st.total_done;
            ts.totalWanted  = st.total_wanted;
            ts.forceStarted  = !(st.flags & lt::torrent_flags::auto_managed)
                               && !(st.flags & lt::torrent_flags::paused);
            ts.queuePosition = static_cast<int>(st.queue_position);
            ts.dlLimit       = rec.handle.download_limit();
            ts.ulLimit       = rec.handle.upload_limit();
        } else {
            ts.stateString = QStringLiteral("unknown");
        }

        result.append(ts);
    }
    return result;
}

QJsonArray TorrentEngine::torrentFiles(const QString& infoHash) const
{
    QJsonArray files;
    QMutexLocker lock(&m_mutex);

    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return files;

    auto ti = it->handle.torrent_file();
    if (!ti) return files;

    auto& fs = ti->files();
    auto fileProgress = it->handle.file_progress();
    auto priorities   = it->handle.get_file_priorities();

    for (int i = 0; i < fs.num_files(); ++i) {
        QJsonObject f;
        f["index"]    = i;
        f["name"]     = QString::fromStdString(fs.file_path(i));
        f["size"]     = static_cast<qint64>(fs.file_size(i));
        f["progress"] = (fs.file_size(i) > 0 && i < static_cast<int>(fileProgress.size()))
                        ? static_cast<double>(fileProgress[i]) / fs.file_size(i)
                        : 0.0;
        f["priority"] = (i < static_cast<int>(priorities.size()))
                        ? static_cast<int>(priorities[i])
                        : 4;   // libtorrent default when out of range
        files.append(f);
    }
    return files;
}

void TorrentEngine::renameFile(const QString& infoHash, int fileIndex, const QString& newName)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    if (fileIndex < 0) return;
    it->handle.rename_file(lt::file_index_t{fileIndex}, newName.toStdString());
}

// ── Tracker management (Phase 6.3) ─────────────────────────────────────────

QList<TrackerInfo> TorrentEngine::trackersFor(const QString& infoHash) const
{
    QList<TrackerInfo> result;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return result;

    const auto trackers = it->handle.trackers();
    const auto now = lt::clock_type::now();
    for (const auto& ae : trackers) {
        TrackerInfo ti;
        ti.url  = QString::fromStdString(ae.url);
        ti.tier = ae.tier;

        // Aggregate across endpoints — take the first populated info_hash
        // entry. libtorrent splits state per-endpoint + per-protocol-version;
        // for UI display the first is close enough.
        bool populated = false;
        for (const auto& ep : ae.endpoints) {
            for (const auto& ih : ep.info_hashes) {
                if (ih.updating)           ti.status = QStringLiteral("Updating");
                else if (ih.last_error)    ti.status = QStringLiteral("Error");
                else if (ih.fails > 0)     ti.status = QStringLiteral("Error");
                else if (ih.start_sent)    ti.status = QStringLiteral("Working");
                else                       ti.status = QStringLiteral("Not contacted");

                const auto secsNext = std::chrono::duration_cast<std::chrono::seconds>(
                    ih.next_announce - now).count();
                const auto secsMin = std::chrono::duration_cast<std::chrono::seconds>(
                    ih.min_announce - now).count();
                if (ih.next_announce.time_since_epoch().count() != 0)
                    ti.nextAnnounce = QDateTime::currentDateTime().addSecs(secsNext);
                if (ih.min_announce.time_since_epoch().count() != 0)
                    ti.minAnnounce = QDateTime::currentDateTime().addSecs(secsMin);

                ti.peers      = ih.scrape_incomplete;
                ti.seeds      = ih.scrape_complete;
                ti.leeches    = ih.scrape_incomplete;
                ti.downloaded = ih.scrape_downloaded;
                ti.message    = QString::fromStdString(ih.message);
                if (ti.message.isEmpty() && ih.last_error)
                    ti.message = QString::fromStdString(ih.last_error.message());
                populated = true;
                break;
            }
            if (populated) break;
        }
        if (!populated)
            ti.status = QStringLiteral("Not contacted");

        result.append(ti);
    }
    return result;
}

void TorrentEngine::addTracker(const QString& infoHash, const QString& url, int tier)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    lt::announce_entry ae(url.toStdString());
    ae.tier = tier;
    it->handle.add_tracker(ae);
}

void TorrentEngine::removeTracker(const QString& infoHash, const QString& url)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    auto current = it->handle.trackers();
    std::vector<lt::announce_entry> kept;
    kept.reserve(current.size());
    const std::string target = url.toStdString();
    for (auto& ae : current) {
        if (ae.url != target) kept.push_back(std::move(ae));
    }
    it->handle.replace_trackers(kept);
}

void TorrentEngine::editTrackerUrl(const QString& infoHash,
                                   const QString& oldUrl, const QString& newUrl)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    auto current = it->handle.trackers();
    const std::string target = oldUrl.toStdString();
    const std::string replacement = newUrl.toStdString();
    for (auto& ae : current) {
        if (ae.url == target) ae.url = replacement;
    }
    it->handle.replace_trackers(current);
}

// ── Peer management (Phase 6.4) ────────────────────────────────────────────

QList<PeerInfo> TorrentEngine::peersFor(const QString& infoHash) const
{
    QList<PeerInfo> result;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return result;

    std::vector<lt::peer_info> peers;
    it->handle.get_peer_info(peers);
    for (const auto& p : peers) {
        PeerInfo pi;
        pi.address = QString::fromStdString(p.ip.address().to_string());
        pi.port    = p.ip.port();
        pi.client  = QString::fromStdString(p.client);
        pi.country = QStringLiteral("--");  // GeoIP out of scope per identity ceiling

        QStringList fl;
        if (p.flags & lt::peer_info::remote_interested) fl << QStringLiteral("I");
        if (p.flags & lt::peer_info::rc4_encrypted)     fl << QStringLiteral("E");
        if (p.flags & lt::peer_info::utp_socket)        fl << QStringLiteral("uTP");
        pi.flags = fl.join(' ');

        pi.connection = (p.flags & lt::peer_info::utp_socket)
            ? QStringLiteral("uTP") : QStringLiteral("TCP");
        pi.progress   = p.progress;
        pi.downSpeed  = p.down_speed;
        pi.upSpeed    = p.up_speed;
        pi.downloaded = p.total_download;
        pi.uploaded   = p.total_upload;
        pi.relevance  = p.progress;   // libtorrent 2.x dropped the dedicated relevance field
        result.append(pi);
    }
    return result;
}

void TorrentEngine::banPeer(const QString& ipAddr)
{
    // Update the persisted list; apply to session filter.
    {
        QSettings s;
        QStringList list = s.value(QStringLiteral("tankorent/bannedPeers")).toStringList();
        if (!list.contains(ipAddr)) list.append(ipAddr);
        s.setValue(QStringLiteral("tankorent/bannedPeers"), list);
    }

    QMutexLocker lock(&m_mutex);
    lt::ip_filter filter = m_session.get_ip_filter();
    lt::error_code ec;
    auto addr = lt::make_address(ipAddr.toStdString(), ec);
    if (ec) return;
    filter.add_rule(addr, addr, lt::ip_filter::blocked);
    m_session.set_ip_filter(filter);
}

void TorrentEngine::addPeer(const QString& infoHash, const QString& ipPort)
{
    const int sep = ipPort.lastIndexOf(':');
    if (sep <= 0) return;
    const QString host = ipPort.left(sep);
    const quint16 port = ipPort.mid(sep + 1).toUShort();
    if (port == 0) return;

    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    lt::error_code ec;
    auto addr = lt::make_address(host.toStdString(), ec);
    if (ec) return;
    it->handle.connect_peer({addr, port});
}

QStringList TorrentEngine::bannedPeers() const
{
    return QSettings().value(QStringLiteral("tankorent/bannedPeers")).toStringList();
}

// ── General tab convenience wrapper (Phase 6.5) ────────────────────────────

TorrentDetails TorrentEngine::torrentDetails(const QString& infoHash) const
{
    TorrentDetails d;
    d.infoHash = infoHash;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return d;

    auto st = it->handle.status();
    d.savePath  = QString::fromStdString(st.save_path);
    d.name      = QString::fromStdString(st.name);
    d.totalSize = st.total_wanted;

    const float uploaded   = static_cast<float>(st.all_time_upload);
    const float downloaded = static_cast<float>(st.all_time_download);
    d.shareRatio = (downloaded > 0.f) ? uploaded / downloaded : 0.f;

    const auto nextSecs = std::chrono::duration_cast<std::chrono::seconds>(
        st.next_announce).count();
    if (nextSecs > 0)
        d.nextReannounce = QDateTime::currentDateTime().addSecs(nextSecs);

    auto ti = it->handle.torrent_file();
    if (ti) {
        d.pieceCount = ti->num_pieces();
        d.pieceSize  = ti->piece_length();
        d.createdBy  = QString::fromStdString(ti->creator());
        d.comment    = QString::fromStdString(ti->comment());
        const auto creationUnix = ti->creation_date();
        if (creationUnix > 0)
            d.created = QDateTime::fromSecsSinceEpoch(creationUnix);
    }

    const auto trackers = it->handle.trackers();
    for (const auto& ae : trackers) {
        for (const auto& ep : ae.endpoints) {
            for (const auto& ih : ep.info_hashes) {
                if (ih.start_sent) {
                    d.currentTracker = QString::fromStdString(ae.url);
                    break;
                }
            }
            if (!d.currentTracker.isEmpty()) break;
        }
        if (!d.currentTracker.isEmpty()) break;
    }
    if (d.currentTracker.isEmpty() && !trackers.empty())
        d.currentTracker = QString::fromStdString(trackers.front().url);

    // Availability: average piece availability across the swarm, 0..N range
    // where N is "copies of the file available." Use libtorrent's
    // distributed_copies as a proxy.
    d.availability = st.distributed_copies;

    return d;
}

bool TorrentEngine::haveContiguousBytes(const QString& infoHash, int fileIndex,
                                         qint64 fileOffset, qint64 length) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return false;

    auto ti = it->handle.torrent_file();
    if (!ti) return false;

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return false;

    // Use libtorrent's map_file() to get the piece range for this byte range
    // map_file returns a peer_request with piece, start, length
    lt::peer_request startReq = ti->map_file(fi, fileOffset, 1);
    lt::peer_request endReq = ti->map_file(fi, fileOffset + length - 1, 1);

    int firstPiece = static_cast<int>(startReq.piece);
    int lastPiece = static_cast<int>(endReq.piece);

    for (int p = firstPiece; p <= lastPiece; ++p) {
        if (!it->handle.have_piece(lt::piece_index_t(p)))
            return false;
    }
    return true;
}

void TorrentEngine::flushCache(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.flush_cache();
}

// ── STREAM_PLAYBACK_FIX Phase 2 Batch 2.1 — piece-deadline primitives ─────────

void TorrentEngine::setPieceDeadlines(const QString& infoHash,
                                       const QList<QPair<int, int>>& deadlines)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    auto ti = it->handle.torrent_file();
    if (!ti) return;   // metadata not ready yet; caller should retry after

    const int numPieces = ti->num_pieces();
    for (const auto& [pieceIndex, msFromNow] : deadlines) {
        if (pieceIndex < 0 || pieceIndex >= numPieces) continue;
        // set_piece_deadline(piece, ms) — libtorrent tracks the urgency
        // relative to other pieces; lower ms = more urgent. API accepts
        // repeated calls for the same piece (updates the deadline).
        it->handle.set_piece_deadline(lt::piece_index_t(pieceIndex), msFromNow);
    }
}

void TorrentEngine::clearPieceDeadlines(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.clear_piece_deadlines();
}

QPair<int, int> TorrentEngine::pieceRangeForFileOffset(const QString& infoHash,
                                                        int fileIndex,
                                                        qint64 fileOffset,
                                                        qint64 length) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return {-1, -1};

    auto ti = it->handle.torrent_file();
    if (!ti) return {-1, -1};

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return {-1, -1};
    if (fileOffset < 0 || length <= 0) return {-1, -1};

    const qint64 fileSize = fs.file_size(fi);
    if (fileOffset >= fileSize) return {-1, -1};
    const qint64 effectiveLen = qMin(length, fileSize - fileOffset);

    // Same shape as haveContiguousBytes at line 753 — map_file returns a
    // peer_request {piece, start, length}; we take `piece` for first/last.
    lt::peer_request startReq = ti->map_file(fi, fileOffset, 1);
    lt::peer_request endReq   = ti->map_file(fi, fileOffset + effectiveLen - 1, 1);

    return { static_cast<int>(startReq.piece),
             static_cast<int>(endReq.piece) };
}

qint64 TorrentEngine::contiguousBytesFromOffset(const QString& infoHash,
                                                 int fileIndex,
                                                 qint64 fileOffset) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return 0;

    auto ti = it->handle.torrent_file();
    if (!ti) return 0;

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return 0;

    const qint64 fileSize = fs.file_size(fi);
    if (fileOffset < 0 || fileOffset >= fileSize) return 0;

    // Walk pieces forward from the starting piece. The starting piece may
    // span before `fileOffset` if fileOffset isn't piece-aligned — we only
    // count bytes AT OR AFTER fileOffset, bounded by file end.
    lt::peer_request startReq = ti->map_file(fi, fileOffset, 1);
    const int firstPiece = static_cast<int>(startReq.piece);
    const int numPieces  = ti->num_pieces();
    const int pieceLen   = ti->piece_length();

    qint64 counted = 0;
    for (int p = firstPiece; p < numPieces; ++p) {
        if (!it->handle.have_piece(lt::piece_index_t(p))) break;

        // First piece: count from fileOffset to piece end, clamped to file.
        // Subsequent pieces: full piece length, clamped to file tail.
        // Use map_file from the OPPOSITE direction — compute the torrent-
        // absolute byte range this piece covers within the file, then the
        // byte count contributed is min(pieceEnd, fileEnd) - max(pieceStart,
        // fileOffset). For the vast majority of cases (piece fully inside
        // file, no cross-file boundary), this simplifies to full piece.
        const qint64 pieceStartAbs = static_cast<qint64>(p) * pieceLen;
        const qint64 pieceEndAbs   = pieceStartAbs + pieceLen;   // exclusive

        // Translate piece to this file's coordinates. ti->map_file gave us
        // piece + in-piece offset for a file offset; reverse mapping here
        // is done by asking: what file-byte range does this piece cover?
        // Simpler: we know startReq.start (the in-piece byte offset of the
        // requested file byte). So the first-piece contribution is:
        //   pieceLen - startReq.start  (bytes from fileOffset to piece end)
        // but clamped to fileSize - fileOffset.
        qint64 contribution;
        if (p == firstPiece) {
            contribution = qint64(pieceLen) - qint64(startReq.start);
        } else {
            contribution = pieceLen;
        }

        // Clamp so we don't overcount past the file's end (last piece may
        // span into the next file for multi-file torrents, or past EOF).
        const qint64 maxRemaining = fileSize - (fileOffset + counted);
        if (contribution > maxRemaining) contribution = maxRemaining;
        if (contribution <= 0) break;

        counted += contribution;
        if (fileOffset + counted >= fileSize) break;
    }

    // STREAM diagnostic (Agent 4B — temporary trace for stream-head-gate
    // regression; remove after that bug closes). Only logs head-offset
    // polls to avoid spam from non-zero offset range queries.
    if (fileOffset == 0) {
        const bool havePiece0 = it->handle.have_piece(lt::piece_index_t(firstPiece));
        qDebug().nospace() << "[STREAM] contig-from-head infoHash="
            << infoHash.left(8) << " file=" << fileIndex
            << " firstPiece=" << firstPiece << " havePiece0=" << havePiece0
            << " counted=" << counted << " fileSize=" << fileSize;
    }

    return counted;
}

// STREAM_ENGINE_FIX Phase 2.6.1 — per-piece have state. Diagnostic-only;
// const + lock-protected. Returns false on unknown infoHash, invalid handle,
// out-of-range pieceIdx, or any libtorrent error path. Same have_piece()
// semantics as the loop in contiguousBytesFromOffset above (line 1169) —
// piece is "have" only after fully downloaded + written to disk.
bool TorrentEngine::havePiece(const QString& infoHash, int pieceIdx) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return false;
    auto ti = it->handle.torrent_file();
    if (!ti) return false;
    if (pieceIdx < 0 || pieceIdx >= ti->num_pieces()) return false;
    return it->handle.have_piece(lt::piece_index_t(pieceIdx));
}

// STREAM_ENGINE_FIX Phase 2.6.3 — per-piece priority boost. Wraps
// libtorrent's set_piece_priority(piece, prio). Used by prepareSeekTarget to
// set priority 7 (max) on seek pieces in addition to the existing
// set_piece_deadline call — combining priority + deadline gives seek pieces
// unambiguous scheduler win. Silent no-op on unknown infoHash / invalid
// pieceIdx / out-of-range priority (libtorrent expects 0..7). Same
// lock-protected pattern as setFilePriorities.
void TorrentEngine::setPiecePriority(const QString& infoHash, int pieceIdx, int priority)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    auto ti = it->handle.torrent_file();
    if (!ti) return;
    if (pieceIdx < 0 || pieceIdx >= ti->num_pieces()) return;
    if (priority < 0 || priority > 7) return;
    it->handle.piece_priority(lt::piece_index_t(pieceIdx),
                              static_cast<lt::download_priority_t>(priority));
}

// MOC needs to see the AlertWorker Q_OBJECT
#include "TorrentEngine.moc"

#else // !HAS_LIBTORRENT — stub implementation so the app compiles without libtorrent

TorrentEngine::TorrentEngine(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir) {}
TorrentEngine::~TorrentEngine() {}
void TorrentEngine::start() { qWarning("TorrentEngine: built without libtorrent"); }
void TorrentEngine::stop() {}
QString TorrentEngine::addMagnet(const QString&, const QString&, bool) { return {}; }
QString TorrentEngine::addFromResume(const QString&, const QString&, bool) { return {}; }
void TorrentEngine::startTorrent(const QString&, const QString&) {}
void TorrentEngine::setFilePriorities(const QString&, const QVector<int>&) {}
void TorrentEngine::renameFile(const QString&, int, const QString&) {}
QList<TrackerInfo> TorrentEngine::trackersFor(const QString&) const { return {}; }
void TorrentEngine::addTracker(const QString&, const QString&, int) {}
void TorrentEngine::removeTracker(const QString&, const QString&) {}
void TorrentEngine::editTrackerUrl(const QString&, const QString&, const QString&) {}
QList<PeerInfo> TorrentEngine::peersFor(const QString&) const { return {}; }
void TorrentEngine::banPeer(const QString&) {}
void TorrentEngine::addPeer(const QString&, const QString&) {}
QStringList TorrentEngine::bannedPeers() const { return {}; }
TorrentDetails TorrentEngine::torrentDetails(const QString&) const { return {}; }
void TorrentEngine::setSequentialDownload(const QString&, bool) {}
void TorrentEngine::flattenFiles(const QString&) {}
void TorrentEngine::resumeTorrent(const QString&) {}
void TorrentEngine::pauseTorrent(const QString&) {}
void TorrentEngine::removeTorrent(const QString&, bool) {}
void TorrentEngine::forceStart(const QString&) {}
void TorrentEngine::forceRecheck(const QString&) {}
void TorrentEngine::forceReannounce(const QString&) {}
void TorrentEngine::queuePositionUp(const QString&) {}
void TorrentEngine::queuePositionDown(const QString&) {}
void TorrentEngine::setQueueLimits(int, int, int) {}
void TorrentEngine::setSpeedLimits(const QString&, int, int) {}
void TorrentEngine::setGlobalSpeedLimits(int, int) {}
void TorrentEngine::setSeedingRules(const QString&, float, int) {}
void TorrentEngine::setGlobalSeedingRules(float, int) {}
QList<TorrentStatus> TorrentEngine::allStatuses() const { return {}; }
QJsonArray TorrentEngine::torrentFiles(const QString&) const { return {}; }
bool TorrentEngine::haveContiguousBytes(const QString&, int, qint64, qint64) const { return false; }
bool TorrentEngine::havePiece(const QString&, int) const { return false; }
void TorrentEngine::setPiecePriority(const QString&, int, int) {}
void TorrentEngine::flushCache(const QString&) {}
// STREAM_PLAYBACK_FIX Phase 2 Batch 2.1 stubs — match header decls.
void TorrentEngine::setPieceDeadlines(const QString&, const QList<QPair<int, int>>&) {}
void TorrentEngine::clearPieceDeadlines(const QString&) {}
QPair<int, int> TorrentEngine::pieceRangeForFileOffset(const QString&, int, qint64, qint64) const { return {-1, -1}; }
qint64 TorrentEngine::contiguousBytesFromOffset(const QString&, int, qint64) const { return 0; }

#endif
