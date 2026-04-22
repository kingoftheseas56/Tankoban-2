#pragma once

#include <QFileInfo>
#include <QRegularExpression>
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

// VIDEO_PLAYER_UI_POLISH Phase 2 2026-04-22 — turn a raw release filename
// into a human-readable "Show · S01E02 · Title" or "Show · Episode N"
// label. Fixes audit finding #3 (raw [DB]Saiki_-_04_(Dual Audio_10bit_
// BD1080p_x265) leaking into bottom title strip + playlist drawer rows).
//
// Patterns handled:
//   [DB]Saiki Kusuo no Psi-nan_-_04_(Dual Audio_10bit_BD1080p_x265)
//     -> "Saiki Kusuo no Psi-nan · Episode 4"
//   [SubsPlease] One Piece - 1040 (1080p) [C4C2F428]
//     -> "One Piece · Episode 1040"
//   The Boys (2019) - S05E03 - My Way
//     -> "The Boys · S05E03 · My Way"
//   Sopranos.S06E09.The.Ride.1080p
//     -> "Sopranos · S06E09 · The Ride"
//   Chainsaw.Man.The.Movie.Reze.Arc.2025.1080p.BluRay.x264
//     -> "Chainsaw Man The Movie Reze Arc"
//
// Fallback: if no known pattern matches, returns the baseName with common
// separators (`.`, `_`) normalized to spaces — always a readable string
// even for unusual releases. Never returns empty as long as input is
// non-empty.
inline QString episodeLabel(const QString& pathOrName) {
    QString base = QFileInfo(pathOrName).completeBaseName();
    if (base.isEmpty()) return pathOrName;

    // Strip ALL leading bracketed/paren release-group tags:
    // "[DB]Saiki...", "[SubsPlease] Show...", "(2019) Show..."
    QRegularExpression leadTag(
        QStringLiteral("^\\s*(?:\\[[^\\]]*\\]|\\([^\\)]*\\))\\s*"));
    while (true) {
        auto m = leadTag.match(base);
        if (!m.hasMatch()) break;
        base.remove(0, m.capturedLength());
    }

    // Strip trailing bracketed quality/hash tags. Repeat because releases
    // often chain several: "... (1080p) [C4C2F428] [HEVC]".
    QRegularExpression trailTag(
        QStringLiteral("\\s*(?:\\[[^\\]]*\\]|\\([^\\)]*\\))\\s*$"));
    while (true) {
        auto m = trailTag.match(base);
        if (!m.hasMatch()) break;
        base.truncate(m.capturedStart());
    }

    // Normalize dot/underscore separators to spaces (.S06E09. -> " S06E09 ").
    // Preserve hyphens — many titles include them ("Psi-nan", "Jojo's-").
    base.replace(QChar('.'), QChar(' '));
    base.replace(QChar('_'), QChar(' '));
    base = base.simplified();
    if (base.isEmpty()) return pathOrName;

    // Try SxxExx pattern (TV). Case-insensitive. Captures S + E digits.
    QRegularExpression sxxexx(
        QStringLiteral("\\b(S\\d{1,2}E\\d{1,3})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    auto seMatch = sxxexx.match(base);
    if (seMatch.hasMatch()) {
        QString show = base.left(seMatch.capturedStart()).trimmed();
        show.remove(QRegularExpression(QStringLiteral("[\\s\\-]+$")));
        QString code = seMatch.captured(1).toUpper();
        QString rest = base.mid(seMatch.capturedEnd()).trimmed();
        // Drop quality-ish trailing tokens (1080p, x264, HEVC, WEB-DL, BluRay, ...).
        QRegularExpression qualityTail(
            QStringLiteral("\\b(?:\\d{3,4}p|x26[45]|h26[45]|HEVC|x265|AVC|WEB[-]?DL|BluRay|BDRip|WEBRip|DDP?5?\\.?1|AAC|HDR|10bit|8bit|REPACK|iNTERNAL)\\b.*$"),
            QRegularExpression::CaseInsensitiveOption);
        rest.remove(qualityTail);
        rest.remove(QRegularExpression(QStringLiteral("^[\\s\\-]+")));
        rest = rest.trimmed();
        if (show.isEmpty()) return code + (rest.isEmpty() ? QString() : QStringLiteral(" · ") + rest);
        if (rest.isEmpty()) return show + QStringLiteral(" · ") + code;
        return show + QStringLiteral(" · ") + code + QStringLiteral(" · ") + rest;
    }

    // Try anime-style "Show - NNNN" or "Show_-_NNNN" pattern (NNNN = episode).
    // Require the number to be 1-4 digits AND NOT a 4-digit year in 19xx-20xx.
    QRegularExpression episode(
        QStringLiteral("(?:^|\\s)-\\s+(\\d{1,4})\\b"));
    auto epMatch = episode.match(base);
    if (epMatch.hasMatch()) {
        int n = epMatch.captured(1).toInt();
        const bool looksLikeYear =
            epMatch.captured(1).size() == 4 && n >= 1900 && n <= 2100;
        if (!looksLikeYear) {
            QString show = base.left(epMatch.capturedStart()).trimmed();
            return show + QStringLiteral(" · Episode ") + QString::number(n);
        }
    }

    // Last resort: "Show 04" or "Show_04" at very end (no separator dash).
    QRegularExpression tailNum(
        QStringLiteral("\\s(\\d{1,4})$"));
    auto tnMatch = tailNum.match(base);
    if (tnMatch.hasMatch()) {
        int n = tnMatch.captured(1).toInt();
        const bool looksLikeYear =
            tnMatch.captured(1).size() == 4 && n >= 1900 && n <= 2100;
        if (!looksLikeYear) {
            QString show = base.left(tnMatch.capturedStart()).trimmed();
            if (!show.isEmpty())
                return show + QStringLiteral(" · Episode ") + QString::number(n);
        }
    }

    // No episode pattern matched — return cleaned string as-is. Movies,
    // one-offs, and unusual releases land here. Drop trailing quality tokens
    // the same way we do for TV so "Chainsaw Man The Movie Reze Arc 2025
    // 1080p BluRay x264" -> "Chainsaw Man The Movie Reze Arc".
    QRegularExpression movieTail(
        QStringLiteral("\\s+(?:19|20)\\d{2}\\s+.+$"));
    auto mtMatch = movieTail.match(base);
    if (mtMatch.hasMatch()) {
        QString show = base.left(mtMatch.capturedStart()).trimmed();
        if (!show.isEmpty()) return show;
    }
    return base;
}

} // namespace player_utils
