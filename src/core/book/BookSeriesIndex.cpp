#include "core/book/BookSeriesIndex.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

BookSeriesIndex::BookSeriesIndex(const QString& dataDir, QObject* parent)
    : QObject(parent), m_dataDir(dataDir) {}

int BookSeriesIndex::matchScore(const QString& query, const SeriesIndexEntry& e)
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return 0;
    const QString name = e.seriesName.toLower();
    int score = 0;
    if (name == q)                       score = 300;
    else if (name.startsWith(q))         score = 200;
    else if (name.contains(q))           score = 100;
    if (!e.author.isEmpty() && e.author.toLower().contains(q))
        score += 25;
    return score;
}

QList<SeriesIndexEntry> BookSeriesIndex::query(const QString& text, int limit) const
{
    struct Scored { int score; const SeriesIndexEntry* e; };
    QList<Scored> scored;
    scored.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        const int s = matchScore(text, e);
        if (s > 0) scored.append({s, &e});
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const Scored& a, const Scored& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.e->seriesName.compare(b.e->seriesName, Qt::CaseInsensitive) < 0;
        });
    QList<SeriesIndexEntry> out;
    for (const auto& s : scored) {
        if (out.size() >= limit) break;
        out.append(*s.e);
    }
    return out;
}

void BookSeriesIndex::setEntries(const QList<SeriesIndexEntry>& entries, qint64 builtAt)
{
    m_entries = entries;
    m_builtAt = builtAt;
}

void BookSeriesIndex::save() const
{
    QJsonArray arr;
    for (const auto& e : m_entries) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), e.seriesId);
        o.insert(QStringLiteral("name"), e.seriesName);
        o.insert(QStringLiteral("author"), e.author);
        if (!e.genre.isEmpty()) o.insert(QStringLiteral("genre"), e.genre);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("builtAt"), static_cast<double>(m_builtAt));
    root.insert(QStringLiteral("entries"), arr);

    QDir().mkpath(m_dataDir);
    QFile f(m_dataDir + QLatin1Char('/') + QLatin1String(FILENAME));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();
    }
}

bool BookSeriesIndex::loadFromFile(const QString& path)
{
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != kSchemaVersion) return false;

    QList<SeriesIndexEntry> entries;
    const QJsonArray arr = root.value(QStringLiteral("entries")).toArray();
    entries.reserve(arr.size());
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        SeriesIndexEntry e;
        e.seriesId   = o.value(QStringLiteral("id")).toString();
        e.seriesName = o.value(QStringLiteral("name")).toString();
        e.author     = o.value(QStringLiteral("author")).toString();
        e.genre      = o.value(QStringLiteral("genre")).toString();
        if (!e.seriesId.isEmpty() && !e.seriesName.isEmpty())
            entries.append(e);
    }
    m_entries = entries;
    m_builtAt = static_cast<qint64>(root.value(QStringLiteral("builtAt")).toDouble());
    return true;
}

void BookSeriesIndex::load(const QString& bundledResourcePath)
{
    // Prefer the refreshed data-dir copy; fall back to the bundled resource.
    if (loadFromFile(m_dataDir + QLatin1Char('/') + QLatin1String(FILENAME)))
        return;
    loadFromFile(bundledResourcePath);
}
