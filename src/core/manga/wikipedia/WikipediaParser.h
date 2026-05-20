// src/core/manga/wikipedia/WikipediaParser.h
//
// Pure parsing surface for Wikipedia "List of <series> manga volumes" /
// "List of <series> chapters" wikitables. Zero Qt::Network dependency so
// this translation unit can compile into the test target — keep it that
// way: don't add QNetwork* / QObject / signals here. Anything that
// reaches the network lives in WikipediaResolver.{h,cpp}.
//
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 15

#pragma once

#include "core/manga/fandom/FandomTypes.h"

#include <QList>
#include <QString>

namespace tankoban::manga::wikipedia {

// Parse the volume catalog out of a Wikipedia "List of X" page's
// `parse.text["*"]` HTML body. Returns an empty list on any failure path.
QList<tankoban::manga::fandom::FandomVolume>
    parseVolumeTable(const QString& rawHtml);

} // namespace tankoban::manga::wikipedia
