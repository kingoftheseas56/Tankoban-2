#pragma once

#include <QFileInfo>
#include <QString>
#include <QUrl>

// VIDEO_PLAYER_FIX Batch 4.2/4.3 — shared classification helpers used by
// OpenUrlDialog (accept-gate) + VideoPlayer (drag-drop classification) +
// VideoContextMenu (recents entry labeling). Header-only — two tiny pure
// functions, no behavior worth its own TU.
namespace player_utils {

// Returns true when `s` parses as a URL whose scheme is one the sidecar
// can handle as a playback source (http/https over QNetworkAccessManager,
// rtsp/rtmp through FFmpeg protocols, file:// as a local path). Anything
// else (empty, relative, missing scheme, ftp/mailto/etc.) returns false.
inline bool looksLikeUrl(const QString& s) {
    const QUrl u(s.trimmed());
    if (!u.isValid() || u.scheme().isEmpty()) return false;
    const QString scheme = u.scheme().toLower();
    return scheme == "http"  || scheme == "https"
        || scheme == "rtsp"  || scheme == "rtmp"
        || scheme == "file";
}

// Returns true when the path's extension indicates a subtitle file.
// Matches the sidecar's load_external_sub accepted set (SidecarProcess
// keeps the authoritative list; this client-side filter avoids routing
// non-subtitle drops into the subtitle path).
inline bool isSubtitleFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "srt" || ext == "vtt"
        || ext == "ass" || ext == "ssa"
        || ext == "sub";
}

} // namespace player_utils
