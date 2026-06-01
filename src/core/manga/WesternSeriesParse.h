// src/core/manga/WesternSeriesParse.h
#pragma once

#include <QString>
#include <QList>
#include <QJsonArray>

// Pure parse/classify helpers for RCO (rcostation.xyz) Western series pages.
// C++ port of the offline harvester: scripts/comics_catalogue/edition_classify.py
// + parse_rco.py. No network, no UI — keep it that way (it is unit-tested in
// tankoban_tests, which links QtCore only). The live path (ReadComicsScraper)
// and the offline Python harvester MUST stay behaviourally identical so a
// searched-and-added series matches the baked 13.
namespace tankoban::manga::western {

struct SeriesItem {
    QString label;   // human label, e.g. "TPB 25 The End"
    QString href;    // "/Comic/<Series>/<Item>" (query stripped)
};

// Tier: 0 Compendium / 1 Omnibus / 2 TPB / 3 Deluxe / 4 Vol / 99 single-issue|unknown.
// Lower = stronger collected-edition signal. First rule that matches wins.
int  editionTier(const QString& label);

// True if the label denotes a collected edition worth a tile. Strong tiers
// (<=3) always count; the soft Vol tier counts only when no single-issue
// marker ("Issue" / "#N") is present.
bool isCollected(const QString& label);

// Last path segment of an href, dash/whitespace runs -> single spaces.
QString slugToLabel(const QString& href);

// Deduped {label, href} for every item link on a series page, first-seen order.
QList<SeriesItem> parseSeriesItems(const QString& html);

// The one series-hero cover path (host-relative, e.g. "/Uploads/.../x.jpg"), or "".
QString parseSeriesCover(const QString& html);

// The RCO "Summary:" prose block, tags stripped + entities unescaped + collapsed, or "".
QString parseSeriesSummary(const QString& html);

// True when an RCO summary is too thin to trust (< 120 chars) and a fallback
// (Wikipedia) should be attempted.
bool needsSummaryFallback(const QString& summary);

// schema-v2 editions[] from raw items: keep collected only, stable-sort by tier
// ascending (first-seen order preserved within a tier). Each entry:
//   { "label": <slugToLabel>, "href": <item href>, "formatTier": <editionTier> }
QJsonArray buildEditions(const QList<SeriesItem>& items);

} // namespace tankoban::manga::western
