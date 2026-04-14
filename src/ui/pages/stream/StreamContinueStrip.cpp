#include "StreamContinueStrip.h"

#include "core/CoreBridge.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamProgress.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QFile>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>

StreamContinueStrip::StreamContinueStrip(CoreBridge* bridge, StreamLibrary* library,
                                         QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_library(library)
{
    buildUI();
}

void StreamContinueStrip::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_group = new QGroupBox(this);
    m_group->setFlat(true);
    m_group->setStyleSheet("QGroupBox { border: none; margin: 0; padding: 0; }");

    auto* groupLayout = new QVBoxLayout(m_group);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(4);

    auto* label = new QLabel("CONTINUE WATCHING", m_group);
    label->setStyleSheet(
        "color: rgba(255,255,255,0.55); font-size: 12px; font-weight: bold; letter-spacing: 1px;");
    groupLayout->addWidget(label);

    m_strip = new TileStrip(m_group);
    m_strip->setMode("continue");
    groupLayout->addWidget(m_strip);

    m_group->hide();
    root->addWidget(m_group);
}

void StreamContinueStrip::refresh()
{
    m_strip->clear();

    QJsonObject allProgress = m_bridge->allProgress("stream");
    if (allProgress.isEmpty()) {
        m_group->hide();
        return;
    }

    // Collect in-progress episodes: positionSec > 10, not finished
    // Group by IMDB ID, keep only the most recently updated per show
    struct ContinueItem {
        QString epKey;
        QString imdbId;
        int season = 0;
        int episode = 0;
        double positionSec = 0;
        double durationSec = 0;
        double percent = 0;
        qint64 updatedAt = 0;
    };

    QHash<QString, ContinueItem> bestPerShow;

    for (auto it = allProgress.begin(); it != allProgress.end(); ++it) {
        QString key = it.key();
        if (!key.startsWith("stream:"))
            continue;

        QJsonObject state = it->toObject();
        double pos = state.value("positionSec").toDouble(0);
        if (pos < MIN_POSITION_SEC)
            continue;
        if (StreamProgress::isFinished(state))
            continue;

        double dur = state.value("durationSec").toDouble(0);
        double pct = StreamProgress::percent(state);
        qint64 updated = state.value("updatedAt").toInteger(0);

        // Parse key: "stream:tt1234567" or "stream:tt1234567:s1:e3"
        QStringList parts = key.split(':');
        QString imdbId;
        int season = 0, episode = 0;

        if (parts.size() >= 2)
            imdbId = parts[1];
        if (parts.size() >= 4) {
            season = parts[2].mid(1).toInt();   // "s1" → 1
            episode = parts[3].mid(1).toInt();  // "e3" → 3
        }

        if (imdbId.isEmpty() || !m_library->has(imdbId))
            continue;

        // Keep the most recently updated episode per show
        auto existing = bestPerShow.find(imdbId);
        if (existing == bestPerShow.end() || updated > existing->updatedAt) {
            ContinueItem item;
            item.epKey = key;
            item.imdbId = imdbId;
            item.season = season;
            item.episode = episode;
            item.positionSec = pos;
            item.durationSec = dur;
            item.percent = pct;
            item.updatedAt = updated;
            bestPerShow[imdbId] = item;
        }
    }

    if (bestPerShow.isEmpty()) {
        m_group->hide();
        return;
    }

    // Sort by updatedAt descending, limit to MAX_ITEMS
    QList<ContinueItem> items = bestPerShow.values();
    std::sort(items.begin(), items.end(), [](const ContinueItem& a, const ContinueItem& b) {
        return a.updatedAt > b.updatedAt;
    });
    if (items.size() > MAX_ITEMS)
        items.resize(MAX_ITEMS);

    // Poster cache path
    QString posterDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + "/Tankoban/data/stream_posters";

    for (const auto& item : items) {
        StreamLibraryEntry entry = m_library->get(item.imdbId);

        // Subtitle: S01E03 or "Movie"
        QString subtitle;
        if (item.season > 0 && item.episode > 0)
            subtitle = QString("S%1E%2").arg(item.season, 2, 10, QChar('0'))
                                         .arg(item.episode, 2, 10, QChar('0'));
        else
            subtitle = "Movie";

        // Poster
        QString posterPath = posterDir + "/" + item.imdbId + ".jpg";
        if (!QFile::exists(posterPath))
            posterPath.clear();

        auto* card = new TileCard(posterPath, entry.name, subtitle);
        card->setProperty("imdbId", item.imdbId);
        card->setProperty("season", item.season);
        card->setProperty("episode", item.episode);

        int pctInt = static_cast<int>(item.percent);
        card->setBadges(item.percent / 100.0, subtitle,
                        QString::number(pctInt) + "%", "reading");

        // Single-click to resume (continue strip behavior)
        connect(card, &TileCard::clicked, this, [this, card]() {
            QString imdb = card->property("imdbId").toString();
            int s = card->property("season").toInt();
            int e = card->property("episode").toInt();
            if (!imdb.isEmpty())
                emit playRequested(imdb, s, e);
        });

        m_strip->addTile(card);
    }

    m_group->show();
}
