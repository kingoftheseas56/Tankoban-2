#include "TorrentEngine.h"

#ifdef HAS_LIBTORRENT

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QDebug>
#include <QTimer>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>

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

        QMutexLocker lock(&m_engine->m_mutex);
        for (auto& rec : m_engine->m_records) {
            if (!rec.handle.is_valid()) continue;
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

    sp.set_int(lt::settings_pack::alert_mask,
               lt::alert_category::status
               | lt::alert_category::storage
               | lt::alert_category::error);

    sp.set_int(lt::settings_pack::connections_limit, 200);

    // Download mode: less aggressive than streaming
    sp.set_int(lt::settings_pack::active_downloads, 5);
    sp.set_int(lt::settings_pack::active_seeds, 5);
    sp.set_int(lt::settings_pack::active_limit, 10);

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
    auto torrents = m_session.get_torrents();
    int count = 0;
    for (auto& h : torrents) {
        if (!h.is_valid()) continue;
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

    for (int i = 0; i < fs.num_files(); ++i) {
        QJsonObject f;
        f["index"]    = i;
        f["name"]     = QString::fromStdString(fs.file_path(i));
        f["size"]     = static_cast<qint64>(fs.file_size(i));
        f["progress"] = (fs.file_size(i) > 0 && i < static_cast<int>(fileProgress.size()))
                        ? static_cast<double>(fileProgress[i]) / fs.file_size(i)
                        : 0.0;
        files.append(f);
    }
    return files;
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
void TorrentEngine::flushCache(const QString&) {}

#endif
