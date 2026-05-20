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

// ─── stream_groups + stream_group_items CRUD ─────────────────────────────

namespace {

// TU-local projection from an executed SELECT against stream_groups. Column
// order must match kStreamGroupSelectCols below to keep get / list paths in
// lockstep.
static StreamGroupRow streamGroupFromQuery(QSqlQuery& q) {
    StreamGroupRow r;
    r.groupId         = q.value(0).toString();
    r.imdbId          = q.value(1).toString();
    r.season          = q.value(2).toInt();
    r.label           = q.value(3).toString();
    r.state           = q.value(4).toString();
    r.retryGeneration = q.value(5).toInt();
    r.stagingPath     = q.value(6).toString();
    r.createdAt       = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
    r.packMode        = q.value(8).toInt() != 0;
    return r;
}

// Same idea for stream_group_items. info_hash is read back as an empty string
// when the underlying column is SQL NULL (which is what happens after the
// referenced torrents row is deleted via ON DELETE SET NULL).
static StreamGroupItemRow streamGroupItemFromQuery(QSqlQuery& q) {
    StreamGroupItemRow r;
    r.groupId      = q.value(0).toString();
    r.itemId       = q.value(1).toString();
    r.episode      = q.value(2).toInt();
    r.infoHash     = q.value(3).toString();
    r.state        = q.value(4).toString();
    r.errorMessage = q.value(5).toString();
    r.fileIndex    = q.value(6).toInt();
    return r;
}

static const char* kStreamGroupSelectCols =
    "group_id, imdb_id, season, label, state, retry_generation, "
    "staging_path, created_at, pack_mode";

static const char* kStreamGroupItemSelectCols =
    "group_id, item_id, episode, info_hash, state, error_message, file_index";

}  // namespace

bool TorrentRepository::upsertStreamGroup(const StreamGroupRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO stream_groups (
            group_id, imdb_id, season, label, state, retry_generation,
            staging_path, created_at, pack_mode
        ) VALUES (
            :group_id, :imdb_id, :season, :label, :state, :retry_generation,
            :staging_path, :created_at, :pack_mode
        )
        ON CONFLICT(group_id) DO UPDATE SET
            imdb_id = excluded.imdb_id,
            season = excluded.season,
            label = excluded.label,
            state = excluded.state,
            retry_generation = excluded.retry_generation,
            staging_path = excluded.staging_path,
            created_at = excluded.created_at,
            pack_mode = excluded.pack_mode
    )SQL"));
    q.bindValue(QStringLiteral(":group_id"), row.groupId);
    q.bindValue(QStringLiteral(":imdb_id"), row.imdbId);
    q.bindValue(QStringLiteral(":season"), row.season);
    q.bindValue(QStringLiteral(":label"), row.label);
    q.bindValue(QStringLiteral(":state"), row.state);
    q.bindValue(QStringLiteral(":retry_generation"), row.retryGeneration);
    q.bindValue(QStringLiteral(":staging_path"), row.stagingPath);
    q.bindValue(QStringLiteral(":created_at"), row.createdAt.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":pack_mode"), row.packMode ? 1 : 0);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertStreamGroup failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeStreamGroup(const QString& groupId) {
    if (!m_open) return false;
    // FK on stream_group_items(group_id) is ON DELETE CASCADE, so this single
    // DELETE sweeps the items rows as well — verified empirically in
    // tests/core/torrent/test_torrent_repository_groups.cpp RemoveGroupCascadesItems.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM stream_groups WHERE group_id = :gid"));
    q.bindValue(QStringLiteral(":gid"), groupId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeStreamGroup failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<StreamGroupRow> TorrentRepository::getStreamGroup(const QString& groupId) {
    if (!m_open) return std::nullopt;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM stream_groups WHERE group_id = :gid")
                  .arg(QString::fromUtf8(kStreamGroupSelectCols)));
    q.bindValue(QStringLiteral(":gid"), groupId);
    if (!q.exec() || !q.next()) return std::nullopt;
    return streamGroupFromQuery(q);
}

std::vector<StreamGroupRow> TorrentRepository::listStreamGroups() {
    std::vector<StreamGroupRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM stream_groups")
                    .arg(QString::fromUtf8(kStreamGroupSelectCols)))) {
        qWarning() << "[TorrentRepository] listStreamGroups failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamGroupFromQuery(q));
    return out;
}

std::vector<StreamGroupRow> TorrentRepository::listStreamGroupsByImdbSeason(const QString& imdbId,
                                                                           int season) {
    std::vector<StreamGroupRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM stream_groups WHERE imdb_id = :imdb AND season = :season")
                  .arg(QString::fromUtf8(kStreamGroupSelectCols)));
    q.bindValue(QStringLiteral(":imdb"), imdbId);
    q.bindValue(QStringLiteral(":season"), season);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listStreamGroupsByImdbSeason failed:"
                   << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamGroupFromQuery(q));
    return out;
}

bool TorrentRepository::upsertStreamGroupItem(const StreamGroupItemRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO stream_group_items (
            group_id, item_id, episode, info_hash, state, error_message, file_index
        ) VALUES (
            :group_id, :item_id, :episode, :info_hash, :state, :error_message, :file_index
        )
        ON CONFLICT(group_id, item_id) DO UPDATE SET
            episode = excluded.episode,
            info_hash = excluded.info_hash,
            state = excluded.state,
            error_message = excluded.error_message,
            file_index = excluded.file_index
    )SQL"));
    q.bindValue(QStringLiteral(":group_id"), row.groupId);
    q.bindValue(QStringLiteral(":item_id"), row.itemId);
    q.bindValue(QStringLiteral(":episode"), row.episode);
    // Bind SQL NULL (not '') when infoHash is empty so the FK constraint stays
    // satisfied and the ON DELETE SET NULL action has a target to overwrite.
    if (row.infoHash.isEmpty()) {
        q.bindValue(QStringLiteral(":info_hash"), QVariant());
    } else {
        q.bindValue(QStringLiteral(":info_hash"), row.infoHash.toLower());
    }
    q.bindValue(QStringLiteral(":state"), row.state);
    q.bindValue(QStringLiteral(":error_message"), row.errorMessage);
    q.bindValue(QStringLiteral(":file_index"), row.fileIndex);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertStreamGroupItem failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeStreamGroupItem(const QString& groupId, const QString& itemId) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM stream_group_items WHERE group_id = :gid AND item_id = :iid"));
    q.bindValue(QStringLiteral(":gid"), groupId);
    q.bindValue(QStringLiteral(":iid"), itemId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeStreamGroupItem failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::vector<StreamGroupItemRow> TorrentRepository::listStreamGroupItems(const QString& groupId) {
    std::vector<StreamGroupItemRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM stream_group_items WHERE group_id = :gid")
                  .arg(QString::fromUtf8(kStreamGroupItemSelectCols)));
    q.bindValue(QStringLiteral(":gid"), groupId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listStreamGroupItems failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamGroupItemFromQuery(q));
    return out;
}

// ─── stream_downloads_index CRUD ─────────────────────────────────────────────

namespace {

// TU-local projection from an executed SELECT against stream_downloads_index.
// Column order must match kStreamDownloadSelectCols below to keep get / list
// paths in lockstep.
static StreamDownloadRow streamDownloadFromQuery(QSqlQuery& q) {
    StreamDownloadRow r;
    r.canonicalPath = q.value(0).toString();
    r.imdbId        = q.value(1).toString();
    r.season        = q.value(2).toInt();
    r.episode       = q.value(3).toInt();
    r.state         = q.value(4).toString();
    r.infoHash      = q.value(5).toString();
    r.addedAt       = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    return r;
}

static const char* kStreamDownloadSelectCols =
    "canonical_path, imdb_id, season, episode, state, info_hash, added_at";

}  // namespace

bool TorrentRepository::upsertStreamDownload(const StreamDownloadRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO stream_downloads_index (
            canonical_path, imdb_id, season, episode, state, info_hash, added_at
        ) VALUES (
            :canonical_path, :imdb_id, :season, :episode, :state, :info_hash, :added_at
        )
        ON CONFLICT(canonical_path) DO UPDATE SET
            imdb_id = excluded.imdb_id,
            season = excluded.season,
            episode = excluded.episode,
            state = excluded.state,
            info_hash = excluded.info_hash,
            added_at = excluded.added_at
    )SQL"));
    q.bindValue(QStringLiteral(":canonical_path"), row.canonicalPath);
    q.bindValue(QStringLiteral(":imdb_id"), row.imdbId);
    q.bindValue(QStringLiteral(":season"), row.season);
    q.bindValue(QStringLiteral(":episode"), row.episode);
    q.bindValue(QStringLiteral(":state"), row.state);
    // Bind SQL NULL (not '') when infoHash is empty so the FK constraint stays
    // satisfied and the ON DELETE SET NULL action has a target to overwrite.
    if (row.infoHash.isEmpty()) {
        q.bindValue(QStringLiteral(":info_hash"), QVariant());
    } else {
        q.bindValue(QStringLiteral(":info_hash"), row.infoHash.toLower());
    }
    q.bindValue(QStringLiteral(":added_at"), row.addedAt.toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertStreamDownload failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeStreamDownload(const QString& canonicalPath) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM stream_downloads_index WHERE canonical_path = :cp"));
    q.bindValue(QStringLiteral(":cp"), canonicalPath);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeStreamDownload failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<StreamDownloadRow> TorrentRepository::getStreamDownload(const QString& canonicalPath) {
    if (!m_open) return std::nullopt;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM stream_downloads_index WHERE canonical_path = :cp")
                  .arg(QString::fromUtf8(kStreamDownloadSelectCols)));
    q.bindValue(QStringLiteral(":cp"), canonicalPath);
    if (!q.exec() || !q.next()) return std::nullopt;
    return streamDownloadFromQuery(q);
}

std::vector<StreamDownloadRow> TorrentRepository::listStreamDownloads() {
    std::vector<StreamDownloadRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM stream_downloads_index")
                    .arg(QString::fromUtf8(kStreamDownloadSelectCols)))) {
        qWarning() << "[TorrentRepository] listStreamDownloads failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamDownloadFromQuery(q));
    return out;
}

std::vector<StreamDownloadRow> TorrentRepository::listStreamDownloadsByImdb(const QString& imdbId,
                                                                            int season) {
    std::vector<StreamDownloadRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM stream_downloads_index "
                  "WHERE imdb_id = :imdb AND season = :season")
                  .arg(QString::fromUtf8(kStreamDownloadSelectCols)));
    q.bindValue(QStringLiteral(":imdb"), imdbId);
    q.bindValue(QStringLiteral(":season"), season);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listStreamDownloadsByImdb failed:"
                   << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamDownloadFromQuery(q));
    return out;
}

} // namespace tankoban::torrent
