#include "ScannerUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace ScannerUtils {

// ── Ignored directories ──────────────────────────────────────────────

static const QStringList IGNORE_DIRS = {
    "__macosx", "node_modules", ".git", ".svn", ".hg",
    "@eadir", "$recycle.bin", "system volume information"
};

bool isIgnoredDir(const QString& dirName)
{
    if (dirName.startsWith('.'))
        return true;
    return IGNORE_DIRS.contains(dirName.toLower());
}

// ── Subdirectory listing ─────────────────────────────────────────────

QStringList listImmediateSubdirs(const QString& rootPath)
{
    QStringList result;
    QDir root(rootPath);
    if (!root.exists())
        return result;

    const auto entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (!isIgnoredDir(entry.fileName()))
            result.append(entry.absoluteFilePath());
    }
    return result;
}

// ── Recursive file walk with directory pruning ───────────────────────

static void walkFilesRecursive(const QString& dirPath,
                               const QStringList& nameFilters,
                               QStringList& out)
{
    QDir dir(dirPath);

    // Collect matching files at this level
    const auto files = dir.entryInfoList(nameFilters, QDir::Files);
    for (const auto& f : files)
        out.append(f.absoluteFilePath());

    // Recurse into non-ignored subdirectories
    const auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& sub : subdirs) {
        if (!isIgnoredDir(sub.fileName()))
            walkFilesRecursive(sub.absoluteFilePath(), nameFilters, out);
    }
}

QStringList walkFiles(const QString& dirPath, const QStringList& nameFilters)
{
    QStringList result;
    walkFilesRecursive(dirPath, nameFilters, result);
    return result;
}

// ── Group by first-level subdirectory ────────────────────────────────

QMap<QString, QStringList> groupByFirstLevelSubdir(
    const QStringList& rootFolders,
    const QStringList& nameFilters)
{
    QMap<QString, QStringList> result;

    for (const auto& root : rootFolders) {
        // 1. Each immediate subdirectory becomes a group
        const QStringList subdirs = listImmediateSubdirs(root);
        for (const auto& subdir : subdirs) {
            QStringList files = walkFiles(subdir, nameFilters);
            if (!files.isEmpty())
                result[subdir] = files;
        }

        // 2. Loose files directly in root
        QDir rootDir(root);
        const auto looseFiles = rootDir.entryInfoList(nameFilters, QDir::Files);
        for (const auto& f : looseFiles)
            result[root].append(f.absoluteFilePath());
    }

    return result;
}

// ── Name cleanup (port of Python clean_media_folder_title) ───────────

static const QRegularExpression SHOW_TITLE_NOISE_RE(
    QStringLiteral(
        "(?i)\\b("
        "2160p|1080p|720p|480p|x264|x265|h\\.?264|h\\.?265|hevc|10bit|8bit|hdr|dv|"
        "webrip|web[\\s.\\-]?dl|bluray|bdrip|dvdrip|hdtv|remux|aac|dts|ddp\\d?|"
        "proper|repack|extended|unrated|multi|dual[\\s.\\-]?audio|dubbed|subbed|"
        "subs?|nf|amzn|hulu|atvp|max|uhd|complete|batch|season[\\s._\\-]*\\d{1,2}|s\\d{1,2}"
        ")\\b")
);

static const QRegularExpression SEASON_TOKEN_RE(
    QStringLiteral("(?i)\\b(?:season[\\s._\\-]*|s)(\\d{1,2})\\b")
);

static const QRegularExpression BRACKET_CHUNK_RE(
    QStringLiteral("\\[([^\\]]*)\\]|\\(([^\\)]*)\\)|\\{([^\\}]*)\\}")
);

static QList<int> extractSeasonNumbers(const QString& raw)
{
    QList<int> out;
    QSet<int> seen;
    auto it = SEASON_TOKEN_RE.globalMatch(raw);
    while (it.hasNext()) {
        auto match = it.next();
        bool ok = false;
        int number = match.captured(1).toInt(&ok);
        if (!ok || number <= 0 || number > 99 || seen.contains(number))
            continue;
        seen.insert(number);
        out.append(number);
    }
    return out;
}

static QString stripNoiseBracketChunks(const QString& text)
{
    QString result;
    int lastEnd = 0;
    auto it = BRACKET_CHUNK_RE.globalMatch(text);

    while (it.hasNext()) {
        auto match = it.next();
        result += text.mid(lastEnd, match.capturedStart() - lastEnd);

        // Get the inner content of the bracket
        QString inner;
        for (int g = 1; g <= 3; ++g) {
            if (!match.captured(g).isNull()) {
                inner = match.captured(g).trimmed();
                break;
            }
        }

        if (inner.isEmpty()) {
            result += ' ';
        } else {
            QString normalized = inner;
            normalized.replace('_', ' ');
            normalized.replace('.', ' ');
            normalized = normalized.trimmed();

            if (normalized.isEmpty()) {
                result += ' ';
            } else {
                // Drop year-only brackets and noise brackets
                static const QRegularExpression yearOnly(QStringLiteral("^(?:19|20)\\d{2}$"));
                if (yearOnly.match(normalized).hasMatch() ||
                    SHOW_TITLE_NOISE_RE.match(normalized).hasMatch()) {
                    result += ' ';
                } else {
                    result += ' ' + normalized + ' ';
                }
            }
        }

        lastEnd = match.capturedEnd();
    }

    result += text.mid(lastEnd);
    return result;
}

QString cleanMediaFolderTitle(const QString& rawName)
{
    QString raw = rawName.trimmed();
    if (raw.isEmpty())
        return raw;

    QList<int> seasonNumbers = extractSeasonNumbers(raw);

    // Replace underscores and dots with spaces
    QString cleaned = raw;
    cleaned.replace('_', ' ');
    cleaned.replace('.', ' ');

    // Strip bracket content containing noise
    cleaned = stripNoiseBracketChunks(cleaned);

    // Remove quality/release markers
    cleaned.replace(SHOW_TITLE_NOISE_RE, QStringLiteral(" "));

    // Drop trailing release-group suffixes
    static const QRegularExpression trailingGroup(
        QStringLiteral("(?:\\s*-\\s*[A-Za-z0-9]{2,16})+$"));
    cleaned.replace(trailingGroup, QStringLiteral(" "));

    // Drop trailing year
    static const QRegularExpression trailingYear(
        QStringLiteral("\\b(?:19|20)\\d{2}\\b$"));
    cleaned.replace(trailingYear, QStringLiteral(" "));

    // Normalize whitespace and trim
    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));
    cleaned.replace(multiSpace, QStringLiteral(" "));
    static const QRegularExpression trimChars(QStringLiteral("^[\\s\\-._]+|[\\s\\-._]+$"));
    cleaned.replace(trimChars, QString());

    // Re-append season numbers if they were stripped
    if (!seasonNumbers.isEmpty()) {
        QString lower = cleaned.toLower();
        QStringList missing;
        for (int n : seasonNumbers) {
            QString label = QStringLiteral("Season %1").arg(n);
            if (!lower.contains(label.toLower()))
                missing.append(label);
        }
        if (!cleaned.isEmpty() && !missing.isEmpty()) {
            cleaned += ' ' + missing.join(' ');
        } else if (cleaned.isEmpty()) {
            cleaned = missing.join(' ');
        }
    }

    // Final normalize
    cleaned.replace(multiSpace, QStringLiteral(" "));
    cleaned.replace(trimChars, QString());

    if (cleaned.length() < 2)
        return raw;

    return cleaned;
}

} // namespace ScannerUtils
