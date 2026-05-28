#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace tankostream::stream {

// A series is "anime" for catalog-reroute purposes when its Cinemeta meta is
// animation AND originates in Japan. Genre-only would wrongly catch western
// cartoons; country-only would catch live-action Japanese drama.
bool isAnimeSeries(const QStringList& genres, const QString& country);

// Parses the Fribb/anime-lists "anime-list-full.json" array into an
// imdb_id -> kitsu_id lookup. Entries lacking a non-empty imdb_id string or an
// integer kitsu_id are skipped (v1 handles a string imdb_id only).
class AnimeIdMap {
public:
    void loadFromJson(const QByteArray& json);
    std::optional<int> kitsuIdForImdb(const QString& imdbId) const;
    int size() const { return m_imdbToKitsu.size(); }

private:
    QHash<QString, int> m_imdbToKitsu;
};

// The search-confirm fallback resolves a Kitsu candidate by title, then
// confirms it via the imdb_id Anime Kitsu embeds in its meta. Accept only when
// both ids are non-empty and equal -- never trust a title hit alone.
bool confirmsKitsuMatch(const QString& wantedImdb, const QString& kitsuMetaImdb);

// Builds broad batch-torrent search queries for an anime title. Anime is
// torrented as big multi-episode batches ("One Piece 1-1076 1080p"), never as
// "Season N", so we search broad forms and let the indexer rank. Returns four
// forms: bare title, "+1080p", "+Complete", "+Batch".
QStringList buildAnimePackQueries(const QString& title);

}  // namespace tankostream::stream
