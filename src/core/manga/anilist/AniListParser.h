// src/core/manga/anilist/AniListParser.h
#pragma once

#include "AniListTypes.h"

#include <QJsonObject>

namespace tankoban::manga::anilist {

// Parse one AniList Media GraphQL response object into a MediaPreview.
// Input: the `media` object — i.e. `response.data.Page.media[i]` for the
// search query, or `response.data.Media` for the by-id query (both have
// the same shape on the fields we read). Missing fields gracefully
// resolve to default-constructed values (empty strings, empty lists, 0).
//
// Extracted out of AniListClient.cpp anonymous namespace 2026-05-21 to
// enable direct GoogleTest coverage of the JSON parsing logic without
// faking QNetworkReply.
MediaPreview parseMediaPreviewFromJson(const QJsonObject& mediaObj);

} // namespace tankoban::manga::anilist
