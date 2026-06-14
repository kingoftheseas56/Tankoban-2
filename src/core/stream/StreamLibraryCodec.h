#pragma once

// SIX_MODE_RESTRUCTURE Arc 2 (2026-06-07), Task 2 — dep-free JSON <-> struct
// codec for StreamLibraryEntry, extracted out of StreamLibrary so it can be
// unit-tested WITHOUT pulling TorrentClient -> libtorrent into the test link
// (StreamLibrary.cpp's remove()/clear() cascade depends on TorrentClient; the
// pure mapping does not). This header includes StreamLibrary.h only for the
// POD struct definition — StreamLibrary.h itself merely forward-declares
// TorrentClient/StreamDownloadIndex/JsonStore (Qt-only includes), so depending
// on it here adds no libtorrent dependency.
//
// Used by StreamLibrary.cpp at its load()/save() sites.

#include "core/stream/StreamLibrary.h"

class QJsonObject;

QJsonObject        streamLibraryEntryToJson(const StreamLibraryEntry& entry);
StreamLibraryEntry streamLibraryEntryFromJson(const QJsonObject& obj);
