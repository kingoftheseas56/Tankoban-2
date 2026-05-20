// TorrentRepository implementation.
//
// P0.3 establishes lifecycle (open/close/initSchema), transactions, schema_meta
// key-value helpers, and a hasTable diagnostic. CRUD methods on torrents,
// stream_groups, stream_group_items, and stream_downloads_index are stubbed
// here (return false / empty); P0.5 / P0.6 / P0.7 fill them in.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 0.

#include "TorrentRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace tankoban::torrent {

// Schema SQL — split on ';' inside initSchema and run statement-by-statement.
// COLLATE NOCASE on hash columns canonicalises case at the SQLite boundary
// (audit Part B item 13 — hash case drift was already an F9 follow-up concern).
static const char* kSchemaSql = R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS torrents (
    hash TEXT PRIMARY KEY NOT NULL COLLATE NOCASE,
    state TEXT NOT NULL,
    name TEXT NOT NULL DEFAULT '',
    added_at TEXT NOT NULL,
    category TEXT NOT NULL DEFAULT '',
    save_path TEXT NOT NULL DEFAULT '',
    content_layout TEXT NOT NULL DEFAULT '',
    stream_group_id TEXT NOT NULL DEFAULT '',
    sequential INTEGER NOT NULL DEFAULT 0,
    imdb_id TEXT NOT NULL DEFAULT '',
    season INTEGER NOT NULL DEFAULT 0,
    magnet_uri TEXT NOT NULL DEFAULT '',
    legacy_no_magnet INTEGER NOT NULL DEFAULT 0,
    error_message TEXT NOT NULL DEFAULT '',
    resume_data BLOB,
    torrent_file BLOB
);

CREATE INDEX IF NOT EXISTS idx_torrents_state ON torrents(state);
CREATE INDEX IF NOT EXISTS idx_torrents_stream_group ON torrents(stream_group_id);
CREATE INDEX IF NOT EXISTS idx_torrents_imdb_season ON torrents(imdb_id, season);

CREATE TABLE IF NOT EXISTS stream_groups (
    group_id TEXT PRIMARY KEY NOT NULL,
    imdb_id TEXT NOT NULL DEFAULT '',
    season INTEGER NOT NULL DEFAULT 0,
    label TEXT NOT NULL DEFAULT '',
    state TEXT NOT NULL,
    retry_generation INTEGER NOT NULL DEFAULT 0,
    staging_path TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    pack_mode INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_stream_groups_imdb_season ON stream_groups(imdb_id, season);

CREATE TABLE IF NOT EXISTS stream_group_items (
    group_id TEXT NOT NULL,
    item_id TEXT NOT NULL,
    episode INTEGER NOT NULL DEFAULT 0,
    info_hash TEXT COLLATE NOCASE,
    state TEXT NOT NULL,
    error_message TEXT NOT NULL DEFAULT '',
    file_index INTEGER NOT NULL DEFAULT -1,
    PRIMARY KEY (group_id, item_id),
    FOREIGN KEY (group_id) REFERENCES stream_groups(group_id) ON DELETE CASCADE,
    FOREIGN KEY (info_hash) REFERENCES torrents(hash) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_stream_group_items_hash ON stream_group_items(info_hash);

CREATE TABLE IF NOT EXISTS stream_downloads_index (
    canonical_path TEXT PRIMARY KEY NOT NULL,
    imdb_id TEXT NOT NULL DEFAULT '',
    season INTEGER NOT NULL DEFAULT 0,
    episode INTEGER NOT NULL DEFAULT 0,
    state TEXT NOT NULL,
    info_hash TEXT COLLATE NOCASE,
    added_at TEXT NOT NULL,
    FOREIGN KEY (info_hash) REFERENCES torrents(hash) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_stream_downloads_imdb ON stream_downloads_index(imdb_id, season, episode);

CREATE TABLE IF NOT EXISTS schema_meta (
    key TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
);
)SQL";

// ─── ctor / dtor ─────────────────────────────────────────────────────────────

TorrentRepository::TorrentRepository(QObject* parent)
    : QObject(parent) {
    m_connectionName = QStringLiteral("TorrentRepository_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16);
}

TorrentRepository::~TorrentRepository() {
    close();
}

// ─── lifecycle ───────────────────────────────────────────────────────────────

bool TorrentRepository::open(const QString& dbFilePath) {
    if (m_open) return true;

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbFilePath);
    if (!m_db.open()) {
        qWarning() << "[TorrentRepository] open failed:" << m_db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    m_open = true;
    if (!initSchema()) {
        qWarning() << "[TorrentRepository] initSchema failed; closing DB";
        close();
        return false;
    }

    return true;
}

bool TorrentRepository::initSchema() {
    if (!m_open) return false;

    const QString blob = QString::fromUtf8(kSchemaSql);
    const QStringList stmts = blob.split(';', Qt::SkipEmptyParts);
    QSqlQuery q(m_db);
    for (const QString& stmt : stmts) {
        const QString trimmed = stmt.trimmed();
        if (trimmed.isEmpty()) continue;
        if (!q.exec(trimmed)) {
            qWarning() << "[TorrentRepository] schema stmt failed:"
                       << q.lastError().text()
                       << "stmt:" << trimmed.left(120);
            return false;
        }
    }

    if (metaValue(QStringLiteral("schema_version")).isEmpty()) {
        setSchemaVersion(kSchemaVersion);
    }
    return true;
}

void TorrentRepository::close() {
    if (m_db.isOpen()) m_db.close();
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_open = false;
}

bool TorrentRepository::isOpen() const {
    return m_open;
}

// ─── schema meta ─────────────────────────────────────────────────────────────

int TorrentRepository::schemaVersion() {
    const QString v = metaValue(QStringLiteral("schema_version"));
    return v.isEmpty() ? 0 : v.toInt();
}

void TorrentRepository::setSchemaVersion(int version) {
    setMetaValue(QStringLiteral("schema_version"), QString::number(version));
}

QString TorrentRepository::metaValue(const QString& key) {
    if (!m_open) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM schema_meta WHERE key = :key"));
    q.bindValue(QStringLiteral(":key"), key);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toString();
}

void TorrentRepository::setMetaValue(const QString& key, const QString& value) {
    if (!m_open) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO schema_meta (key, value) VALUES (:k, :v) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), value);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] setMetaValue failed:" << q.lastError().text();
    }
}

bool TorrentRepository::hasTable(const QString& tableName) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    q.bindValue(QStringLiteral(":name"), tableName);
    if (!q.exec()) return false;
    return q.next();
}

// ─── transactions ────────────────────────────────────────────────────────────

bool TorrentRepository::beginTransaction() {
    return m_open && m_db.transaction();
}

bool TorrentRepository::commit() {
    return m_open && m_db.commit();
}

bool TorrentRepository::rollback() {
    return m_open && m_db.rollback();
}

// ─── torrents CRUD ───────────────────────────────────────────────────────────
// Implementation in P0.5. Stubs return false / empty.

bool TorrentRepository::upsertTorrent(const TorrentRow&) { return false; }
bool TorrentRepository::updateTorrentState(const QString&, TorrentState, const QString&) { return false; }
bool TorrentRepository::updateTorrentResumeData(const QString&, const QByteArray&) { return false; }
bool TorrentRepository::updateTorrentName(const QString&, const QString&) { return false; }
bool TorrentRepository::updateTorrentSavePath(const QString&, const QString&) { return false; }
bool TorrentRepository::removeTorrent(const QString&) { return false; }
std::optional<TorrentRow> TorrentRepository::getTorrent(const QString&) { return std::nullopt; }
std::vector<TorrentRow> TorrentRepository::listTorrents() { return {}; }
std::vector<TorrentRow> TorrentRepository::listTorrentsByState(TorrentState) { return {}; }
std::vector<TorrentRow> TorrentRepository::listTorrentsByImdb(const QString&, int) { return {}; }
std::vector<TorrentRow> TorrentRepository::listTorrentsByStreamGroup(const QString&) { return {}; }

// ─── stream_groups CRUD ──────────────────────────────────────────────────────
// Implementation in P0.6.

bool TorrentRepository::upsertStreamGroup(const StreamGroupRow&) { return false; }
bool TorrentRepository::removeStreamGroup(const QString&) { return false; }
std::optional<StreamGroupRow> TorrentRepository::getStreamGroup(const QString&) { return std::nullopt; }
std::vector<StreamGroupRow> TorrentRepository::listStreamGroups() { return {}; }
std::vector<StreamGroupRow> TorrentRepository::listStreamGroupsByImdbSeason(const QString&, int) { return {}; }

bool TorrentRepository::upsertStreamGroupItem(const StreamGroupItemRow&) { return false; }
bool TorrentRepository::removeStreamGroupItem(const QString&, const QString&) { return false; }
std::vector<StreamGroupItemRow> TorrentRepository::listStreamGroupItems(const QString&) { return {}; }

// ─── stream_downloads_index CRUD ─────────────────────────────────────────────
// Implementation in P0.7.

bool TorrentRepository::upsertStreamDownload(const StreamDownloadRow&) { return false; }
bool TorrentRepository::removeStreamDownload(const QString&) { return false; }
std::optional<StreamDownloadRow> TorrentRepository::getStreamDownload(const QString&) { return std::nullopt; }
std::vector<StreamDownloadRow> TorrentRepository::listStreamDownloads() { return {}; }
std::vector<StreamDownloadRow> TorrentRepository::listStreamDownloadsByImdb(const QString&, int) { return {}; }

} // namespace tankoban::torrent
