#pragma once

#include <QFrame>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "SidecarProcess.h"
#include "core/stream/addon/SubtitleInfo.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Unified subtitle popover. Replaces both TrackPopover's subtitle
// section AND SubtitleMenu (Tankostream addon-fetched external subs +
// "Load from file..."). Single QListWidget with rows from three
// sources merged by user-visible track identity:
//  - "Off"
//  - Embedded tracks (sidecar listSubtitleTracks + tracks_changed
//    signal)
//  - Addon tracks (StreamPage SubtitlesAggregator → VideoPlayer
//    forwarder → setExternalTracks)
//  - Local file (Load from file... button → QFileDialog)
//
// The popover dispatches choices to the sidecar directly:
//   - Off / Embedded → sendSetSubtitleTrack(idx)
//   - Addon / File   → sendSetSubtitleUrl(url, 0, 0)
// (No offset/delay — those parameters were SubtitleMenu's per-source
// sliders; the unified Settings popover owns global subtitle delay
// instead.)
//
// Embedded selection ALSO emits embeddedSubtitleSelected(int) so
// VideoPlayer can update m_activeSubId, save the user's preferred
// language to QSettings, and write the per-show pref. That signal is
// the drop-in replacement for TrackPopover::subtitleTrackSelected.
class SubtitlePopover : public QFrame
{
    Q_OBJECT

public:
    explicit SubtitlePopover(QWidget* parent = nullptr);

    // Wire the sidecar so the popover stays in sync with embedded
    // tracks on every tracks_changed event and can dispatch the
    // sub-set-track / sub-set-url commands directly.
    void setSidecar(SidecarProcess* sidecar);

    // Called by VideoPlayer when the StreamPage SubtitlesAggregator
    // resolves external subs for the currently-playing stream.
    // Passing empty lists clears the external section.
    void setExternalTracks(const QList<tankostream::addon::SubtitleTrack>& tracks,
                           const QHash<QString, QString>& originByTrackKey);

    // Called by VideoPlayer's tracksUpdated handler — the embedded
    // track JSON arrives via the sidecar's tracks_changed event and
    // VideoPlayer hoists the subtitle subset before forwarding.
    // Pass-through alternative: VideoPlayer can rely on setSidecar's
    // direct subscription to subtitleTracksListed; this method exists
    // for explicit refresh from the parent (mirrors the old
    // TrackPopover::populate API).
    void setEmbeddedTracksFromJson(const QJsonArray& tracks,
                                   int currentSubId, bool subVisible);

    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }

signals:
    // Embedded-track or Off selection. id == 0 means Off; positive
    // id is an embedded track. VideoPlayer's slot updates
    // m_activeSubId + saves preferred-sub-lang QSettings + per-show
    // prefs.
    void embeddedSubtitleSelected(int id);
    void hoverChanged(bool hovered);
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 — fired from dismiss() so
    // VideoPlayer can drive the anchor chip's :checked state in lockstep
    // with popover visibility. Without this signal the chip stays
    // visually checked after item-click / click-outside dismisses, even
    // though the popover is hidden.
    void dismissed();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class ChoiceKind {
        Off,
        Embedded,
        Addon,
        LocalFile,
    };

    struct Choice {
        ChoiceKind kind = ChoiceKind::Off;
        int     embeddedIndex = -1;  // sidecar embedded-track index
        int     embeddedId    = 0;   // populated id (used for embeddedSubtitleSelected)
        QUrl    url;
        QString addonId;
        QString key;
        QString title;
        QString language;
    };

    void buildUI();
    void rebuildChoices();
    void refreshList();

    void onEmbeddedTracksListed(const QList<SubtitleTrackInfo>& tracks, int activeIndex);
    void onChoiceClicked(QListWidgetItem* item);
    void onLoadFileClicked();
    void applyChoice(const Choice& c);

    void dismiss();
    void installClickFilter();
    void removeClickFilter();
    void anchorAbove(QWidget* anchor);

    static QString normalizeAddonKey(const tankostream::addon::SubtitleTrack& t);
    static QString addonDisplayLabel(const tankostream::addon::SubtitleTrack& t);
    static QString embeddedDisplayLabel(const SubtitleTrackInfo& t);

    SidecarProcess* m_sidecar = nullptr;

    QList<SubtitleTrackInfo> m_embeddedTracks;
    int m_activeEmbeddedIndex = -1;

    // Optional VideoPlayer-supplied embedded JSON (used when populate
    // path arrives before the sidecar's listSubtitleTracks cache is
    // ready). When non-empty this takes precedence over m_embeddedTracks.
    QJsonArray m_embeddedJson;
    int  m_currentEmbeddedId = 0;
    bool m_subVisible = true;

    QList<tankostream::addon::SubtitleTrack> m_addonTracks;
    QHash<QString, QString> m_addonOriginsByKey;

    QString m_customFilePath;
    QString m_activeChoiceKey;

    QList<Choice> m_choices;

    QLabel*      m_titleLabel  = nullptr;
    QListWidget* m_choiceList  = nullptr;
    QPushButton* m_loadFileBtn = nullptr;

    bool m_clickFilterInstalled = false;
    QPointer<QWidget> m_anchor;
};
