// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 5.3 (Qt-side subtitle menu)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:252
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:255
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:256
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:257
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9025
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9195
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.1_subtitles_aggregator.cpp:322
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.1_subtitles_aggregator.cpp:488
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.2_sidecar_protocol_extension.cpp:136
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.2_sidecar_protocol_extension.cpp:143
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.2_sidecar_protocol_extension.cpp:170
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.2_sidecar_protocol_extension.cpp:232
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.2_sidecar_protocol_extension.cpp:241
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/5.2_sidecar_protocol_extension.cpp:250
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:294
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:359
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:392
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:432
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:677
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:695
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:714
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:1581
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:1635
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:1644
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.h:21
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.h:36
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoContextMenu.cpp:130
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 5.3.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QFileDialog>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

namespace tankostream::addon {

struct SubtitleTrack {
    QString id;
    QString lang;
    QUrl url;
    QString label;
};

struct Stream {
    struct BehaviorHints {
        QString filename;
        QString videoHash;
        qint64 videoSize = 0;
    };
    BehaviorHints behaviorHints;
};

} // namespace tankostream::addon

namespace tankostream::stream {

struct SubtitleLoadRequest {
    QString type;
    QString id;
    tankostream::addon::Stream selectedStream;
};

class SubtitlesAggregator : public QObject {
    Q_OBJECT
public:
    explicit SubtitlesAggregator(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void load(const SubtitleLoadRequest& request);

signals:
    void subtitlesReady(const QList<tankostream::addon::SubtitleTrack>& tracks,
                        const QHash<QString, QString>& originByTrackKey);
    void subtitlesError(const QString& addonId, const QString& message);
};

} // namespace tankostream::stream

namespace tankoban::prototype::sidecar52 {

struct SidecarSubtitleTrack {
    int index = -1;
    QString sidecarId;
    QString lang;
    QString title;
    bool external = false;
};

class SidecarSubtitleProtocolBridge : public QObject {
    Q_OBJECT
public:
    explicit SidecarSubtitleProtocolBridge(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    QList<SidecarSubtitleTrack> listSubtitleTracks() const;
    void setSubtitleTrack(int index);
    void setSubtitleUrl(const QUrl& url, int offsetMs, int delayMs);
    void setSubtitleOffset(int pixelOffsetY);
    void setSubtitleDelay(int ms);
    void setSubtitleSize(double scale);
};

} // namespace tankoban::prototype::sidecar52

namespace tankoban::prototype::subtitle53 {

using tankoban::prototype::sidecar52::SidecarSubtitleProtocolBridge;
using tankoban::prototype::sidecar52::SidecarSubtitleTrack;
using tankostream::addon::SubtitleTrack;
using tankostream::stream::SubtitleLoadRequest;
using tankostream::stream::SubtitlesAggregator;

enum class SubtitleChoiceKind {
    Off,
    EmbeddedTrack,
    AddonTrack,
    LocalFileTrack,
};

struct SubtitleChoice {
    SubtitleChoiceKind kind = SubtitleChoiceKind::Off;

    // Embedded track path
    int embeddedIndex = -1;

    // Addon/file path
    QUrl url;
    QString addonId;

    // Display
    QString key;
    QString title;
    QString language;
};

// Compact menu state used by both the popover and context-menu builders.
class SubtitleMenuState {
public:
    void setEmbeddedTracks(const QList<SidecarSubtitleTrack>& tracks)
    {
        m_embeddedTracks = tracks;
        rebuildChoices();
    }

    void setAddonTracks(const QList<SubtitleTrack>& tracks,
                        const QHash<QString, QString>& originsByTrackKey)
    {
        m_addonTracks = tracks;
        m_addonOriginsByKey = originsByTrackKey;
        rebuildChoices();
    }

    void setCustomFile(const QString& localPath)
    {
        m_customFilePath = localPath.trimmed();
        rebuildChoices();
    }

    QList<SubtitleChoice> choices() const { return m_choices; }

private:
    static QString normalizeKey(const SubtitleTrack& track)
    {
        return track.id.trimmed().toLower()
            + QLatin1Char('|')
            + track.lang.trimmed().toLower()
            + QLatin1Char('|')
            + track.url.toString(QUrl::FullyEncoded).toLower();
    }

    static QString bestAddonLabel(const SubtitleTrack& track)
    {
        if (!track.label.trimmed().isEmpty()) {
            return track.label.trimmed();
        }
        if (!track.lang.trimmed().isEmpty()) {
            return track.lang.trimmed().toUpper();
        }
        return QStringLiteral("Addon subtitle");
    }

    static QString bestEmbeddedLabel(const SidecarSubtitleTrack& track)
    {
        if (!track.title.trimmed().isEmpty()) {
            return track.title.trimmed();
        }
        if (!track.lang.trimmed().isEmpty()) {
            return track.lang.trimmed().toUpper();
        }
        return QStringLiteral("Embedded subtitle");
    }

    void rebuildChoices()
    {
        m_choices.clear();

        SubtitleChoice off;
        off.kind = SubtitleChoiceKind::Off;
        off.key = QStringLiteral("off");
        off.title = QStringLiteral("Off");
        m_choices.push_back(off);

        for (const SidecarSubtitleTrack& embedded : m_embeddedTracks) {
            SubtitleChoice c;
            c.kind = SubtitleChoiceKind::EmbeddedTrack;
            c.key = QStringLiteral("emb:%1").arg(embedded.index);
            c.embeddedIndex = embedded.index;
            c.title = bestEmbeddedLabel(embedded);
            c.language = embedded.lang.trimmed();
            m_choices.push_back(c);
        }

        for (const SubtitleTrack& addon : m_addonTracks) {
            SubtitleChoice c;
            c.kind = SubtitleChoiceKind::AddonTrack;
            c.key = QStringLiteral("addon:%1")
                        .arg(addon.url.toString(QUrl::FullyEncoded));
            c.url = addon.url;
            c.language = addon.lang.trimmed();
            c.title = bestAddonLabel(addon);
            c.addonId = m_addonOriginsByKey.value(normalizeKey(addon));
            m_choices.push_back(c);
        }

        if (!m_customFilePath.isEmpty()) {
            SubtitleChoice c;
            c.kind = SubtitleChoiceKind::LocalFileTrack;
            c.key = QStringLiteral("file:%1").arg(m_customFilePath);
            c.url = QUrl::fromLocalFile(m_customFilePath);
            c.title = QStringLiteral("Local file");
            m_choices.push_back(c);
        }
    }

    QList<SidecarSubtitleTrack> m_embeddedTracks;
    QList<SubtitleTrack> m_addonTracks;
    QHash<QString, QString> m_addonOriginsByKey;
    QString m_customFilePath;
    QList<SubtitleChoice> m_choices;
};

// New popover for Batch 5.3. Kept separate from TrackPopover so audio-track
// UX can stay as-is while subtitle UX grows to include addon and file sources.
class SubtitleMenuPopover : public QFrame {
    Q_OBJECT

public:
    explicit SubtitleMenuPopover(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("SubtitleMenuPopover"));
        setMinimumWidth(320);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        auto* title = new QLabel(QStringLiteral("Subtitles"), this);
        root->addWidget(title);

        m_choiceList = new QListWidget(this);
        root->addWidget(m_choiceList);

        m_loadFileBtn = new QPushButton(QStringLiteral("Load from file..."), this);
        root->addWidget(m_loadFileBtn);

        auto* delayLabel = new QLabel(QStringLiteral("Delay (ms)"), this);
        root->addWidget(delayLabel);
        m_delaySlider = new QSlider(Qt::Horizontal, this);
        m_delaySlider->setRange(-5000, 5000);
        m_delaySlider->setSingleStep(50);
        root->addWidget(m_delaySlider);

        auto* offsetLabel = new QLabel(QStringLiteral("Offset (px)"), this);
        root->addWidget(offsetLabel);
        m_offsetSlider = new QSlider(Qt::Horizontal, this);
        m_offsetSlider->setRange(-120, 200);
        m_offsetSlider->setSingleStep(2);
        root->addWidget(m_offsetSlider);

        auto* sizeLabel = new QLabel(QStringLiteral("Size (%)"), this);
        root->addWidget(sizeLabel);
        m_sizeSlider = new QSlider(Qt::Horizontal, this);
        m_sizeSlider->setRange(50, 200);
        m_sizeSlider->setSingleStep(5);
        m_sizeSlider->setValue(100);
        root->addWidget(m_sizeSlider);

        connect(m_choiceList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                emit choiceSelected(item->data(Qt::UserRole).toString());
            });
        connect(m_loadFileBtn, &QPushButton::clicked, this, [this]() {
            emit loadFromFileRequested();
        });
        connect(m_delaySlider, &QSlider::valueChanged, this,
            [this](int ms) { emit delayChanged(ms); });
        connect(m_offsetSlider, &QSlider::valueChanged, this,
            [this](int px) { emit offsetChanged(px); });
        connect(m_sizeSlider, &QSlider::valueChanged, this,
            [this](int pct) { emit sizeScaleChanged(pct / 100.0); });
    }

    void setChoices(const QList<SubtitleChoice>& choices)
    {
        m_choiceList->clear();
        for (const SubtitleChoice& c : choices) {
            QString label = c.title;
            if (!c.language.isEmpty()) {
                label += QStringLiteral(" (%1)").arg(c.language.toUpper());
            }
            if (!c.addonId.isEmpty()) {
                label += QStringLiteral("  [%1]").arg(c.addonId);
            }
            auto* item = new QListWidgetItem(label, m_choiceList);
            item->setData(Qt::UserRole, c.key);
            m_choiceList->addItem(item);
        }
    }

signals:
    void choiceSelected(const QString& key);
    void loadFromFileRequested();
    void delayChanged(int ms);
    void offsetChanged(int px);
    void sizeScaleChanged(double scale);

private:
    QListWidget* m_choiceList = nullptr;
    QPushButton* m_loadFileBtn = nullptr;
    QSlider* m_delaySlider = nullptr;
    QSlider* m_offsetSlider = nullptr;
    QSlider* m_sizeSlider = nullptr;
};

// Bridges Batch 5.1 (addon subtitle fetch) and Batch 5.2 (sidecar subtitle
// protocol adapter) into one Qt menu state machine.
class SubtitleMenuController : public QObject {
    Q_OBJECT

public:
    explicit SubtitleMenuController(SubtitlesAggregator* subtitlesAggregator,
                                    SidecarSubtitleProtocolBridge* sidecarBridge,
                                    QWidget* fileDialogParent,
                                    QObject* parent = nullptr)
        : QObject(parent)
        , m_subtitlesAggregator(subtitlesAggregator)
        , m_sidecarBridge(sidecarBridge)
        , m_fileDialogParent(fileDialogParent)
    {
        connect(m_subtitlesAggregator, &SubtitlesAggregator::subtitlesReady, this,
            [this](const QList<SubtitleTrack>& tracks,
                   const QHash<QString, QString>& origins) {
                m_state.setAddonTracks(tracks, origins);
                emit menuStateChanged(m_state.choices());
            });

        connect(m_subtitlesAggregator, &SubtitlesAggregator::subtitlesError, this,
            [this](const QString& addonId, const QString& message) {
                emit nonBlockingWarning(
                    QStringLiteral("[%1] %2").arg(addonId, message));
            });
    }

    void refreshForStream(const QString& type,
                          const QString& id,
                          const tankostream::addon::Stream& selectedStream)
    {
        m_currentType = type;
        m_currentId = id;

        // Pull embedded tracks from sidecar snapshot first so menu is usable
        // immediately while addon subtitle requests are in-flight.
        m_state.setEmbeddedTracks(m_sidecarBridge->listSubtitleTracks());
        emit menuStateChanged(m_state.choices());

        SubtitleLoadRequest req;
        req.type = type;
        req.id = id;
        req.selectedStream = selectedStream;
        m_subtitlesAggregator->load(req);
    }

    void choose(const QString& key)
    {
        const QList<SubtitleChoice> choices = m_state.choices();
        for (const SubtitleChoice& c : choices) {
            if (c.key != key) {
                continue;
            }

            switch (c.kind) {
            case SubtitleChoiceKind::Off:
                m_sidecarBridge->setSubtitleTrack(-1);
                return;
            case SubtitleChoiceKind::EmbeddedTrack:
                m_sidecarBridge->setSubtitleTrack(c.embeddedIndex);
                return;
            case SubtitleChoiceKind::AddonTrack:
            case SubtitleChoiceKind::LocalFileTrack:
                m_sidecarBridge->setSubtitleUrl(c.url, m_offsetPx, m_delayMs);
                return;
            }
        }
    }

    void loadFromFile()
    {
        const QString path = QFileDialog::getOpenFileName(
            m_fileDialogParent,
            QStringLiteral("Load Subtitle"),
            QString(),
            QStringLiteral("Subtitles (*.srt *.ass *.ssa *.sub *.vtt)"));
        if (path.isEmpty()) {
            return;
        }
        m_state.setCustomFile(path);
        emit menuStateChanged(m_state.choices());
        choose(QStringLiteral("file:%1").arg(path));
    }

    void setDelayMs(int ms)
    {
        m_delayMs = ms;
        m_sidecarBridge->setSubtitleDelay(ms);
    }

    void setOffsetPx(int px)
    {
        m_offsetPx = px;
        m_sidecarBridge->setSubtitleOffset(px);
    }

    void setSizeScale(double scale)
    {
        m_sizeScale = scale;
        m_sidecarBridge->setSubtitleSize(scale);
    }

signals:
    void menuStateChanged(const QList<SubtitleChoice>& choices);
    void nonBlockingWarning(const QString& message);

private:
    SubtitlesAggregator* m_subtitlesAggregator = nullptr;
    SidecarSubtitleProtocolBridge* m_sidecarBridge = nullptr;
    QWidget* m_fileDialogParent = nullptr;

    SubtitleMenuState m_state;
    QString m_currentType;
    QString m_currentId;

    int m_delayMs = 0;
    int m_offsetPx = 0;
    double m_sizeScale = 1.0;
};

} // namespace tankoban::prototype::subtitle53

// -----------------------------------------------------------------
// Wiring sketch (Batch 5.3 reference)
// -----------------------------------------------------------------
//
// 1) StreamPage additions (consumes 5.1 result, forwards to player):
//
//    Members:
//      tankostream::stream::SubtitlesAggregator* m_subtitlesAggregator = nullptr;
//      QList<tankostream::addon::SubtitleTrack>  m_externalSubtitleTracks;
//      QHash<QString, QString>                   m_externalSubtitleOrigins;
//      tankostream::addon::Stream               m_lastSelectedStream;
//      QString                                  m_lastStreamType;
//      QString                                  m_lastStreamId;
//
//    In onPlayRequested after StreamPickerDialog accept:
//      m_lastSelectedStream = selected.stream;
//      m_lastStreamType = req.type;
//      m_lastStreamId = req.id;
//      m_subtitlesAggregator->load({m_lastStreamType, m_lastStreamId, m_lastSelectedStream});
//
//    On subtitlesReady:
//      m_externalSubtitleTracks = tracks;
//      m_externalSubtitleOrigins = origins;
//      if (auto* player = window()->findChild<VideoPlayer*>()) {
//          player->setExternalSubtitleTracks(m_externalSubtitleTracks, m_externalSubtitleOrigins);
//      }
//
//    In onReadyToPlay before player->openFile(httpUrl):
//      player->setStreamSubtitleContext(m_lastStreamType, m_lastStreamId, m_lastSelectedStream);
//      player->setExternalSubtitleTracks(m_externalSubtitleTracks, m_externalSubtitleOrigins);
//
// 2) VideoPlayer additions (consumes 5.2 bridge):
//
//    Members:
//      SidecarSubtitleProtocolBridge* m_subBridge = nullptr;
//      SubtitleMenuController*        m_subMenuController = nullptr;
//      SubtitleMenuPopover*           m_subMenu = nullptr;
//
//    Build UI:
//      - Keep existing TrackPopover for audio.
//      - Add SubtitleMenuPopover (opened by "S" shortcut and context menu "Subtitles...").
//      - Hook sliders to setSubtitleDelay/setSubtitleOffset/setSubtitleSize.
//
//    Selection dispatch:
//      Off / embedded -> setSubtitleTrack(index/-1)
//      addon track / file -> setSubtitleUrl(url, offset_ms, delay_ms)
//
//    Existing context menu migration:
//      - Replace SetSubtitleTrack + LoadExternalSub with a single
//        "Open Subtitle Menu" action, or keep both as aliases into
//        SubtitleMenuController::choose/loadFromFile.
//
// 3) Drift-aware note from 5.2:
//
//    If sidecar binary remains prebuilt, SidecarSubtitleProtocolBridge keeps
//    mapping "setSubtitleOffset/setSubtitleSize" onto sendSetSubStyle and
//    "setSubtitleUrl" onto sendLoadExternalSub(tempPath). Batch 5.3 should
//    target bridge methods, not raw SidecarProcess commands.

