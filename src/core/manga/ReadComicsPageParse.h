// src/core/manga/ReadComicsPageParse.h
#pragma once
#include "MangaResult.h"      // PageInfo
#include <QString>
#include <QList>
#include <QPair>

// Pure parse/descramble for the readcomicsonline / rcostation.xyz reader. No
// network, no UI — unit-tested in tankoban_tests. RCO scrambles its blogspot
// page-image URLs (rguard.min.js v1.5.8): an inline JS array + a transpose +
// base64 transform, with a per-page-randomized junk-token replacement. This
// recovers the real URLs. Port of gallery-dl readcomiconline.py baeu()+images()
// (proven live 2026-06-03 — agents/audits/rco_descramble_spike.py, 3/3 JPEGs).
// FRAGILE: the transpose offsets are rguard-version-specific — if rcostation
// bumps the scheme, mirror gallery-dl's readcomiconline.py and re-freeze the
// ReadComicsPageParseTest vector. readallcomics.com (plain blogspot <img> URLs,
// no descramble) is the documented fallback source.
namespace tankoban::manga::readcomics {

// Apply the page's ordered junk-token replacements (l.replace(/X/g,'Y')) to a
// raw token, in order.
QString applyReplacements(const QString& token,
                          const QList<QPair<QString, QString>>& replacements);

// Descramble one (already-replacement-applied) token into a real blogspot URL.
// `root` empty -> https://2.bp.blogspot.com. Mirrors gallery-dl baeu().
QString baeu(const QString& url, const QString& root);

// Parse a reader-page HTML payload into ordered page-image URLs. Extracts the
// root, the array var name, and the replacements from the page, then descrambles
// each token. Returns empty (never throws) if the markers are absent.
QList<PageInfo> parseReaderPages(const QString& html);

} // namespace tankoban::manga::readcomics
