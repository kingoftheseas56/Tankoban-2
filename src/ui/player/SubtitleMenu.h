#pragma once

#include <QFrame>
#include <QHash>
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
class QSlider;

// Batch 5.3 (Tankostream Phase 5) — Qt-side subtitle menu.
//
// Replaces nothing — supplements the existing TrackPopover's subtitle section
// with Tankostream-specific sources (addon-fetched subs + "Load from file…").
// Invoked by the T keyboard shortcut or the player context menu's
// "Open Subtitles menu…" action.
//
// Presentation mirrors TrackPopover (QFrame, anchored above a trigger widget
// via toggle(QWidget*)). Selection dispatches to SidecarProcess's Batch 5.2
// wrappers: embedded tracks via sendSetSubtitleTrack(index), addon/file tracks
// via sendSetSubtitleUrl(url, offsetPx, delayMs).
class SubtitleMenu : public QFrame
{
    Q_OBJECT

public:
    explicit SubtitleMenu(QWidget* parent = nullptr);

    // Wires the sidecar so the menu stays in sync with embedded tracks on
    // every tracks_changed event and can dispatch the 5.2 commands.
    void setSidecar(SidecarProcess* sidecar);

    // Called by StreamPage (via VideoPlayer::setExternalSubtitleTracks) when
    // the SubtitlesAggregator produces results for the currently-playing
    // stream. Passing empty lists clears the external section.
    void setExternalTracks(const QList<tankostream::addon::SubtitleTrack>& tracks,
                           const QHash<QString, QString>& originByTrackKey);

    // TrackPopover-style toggle: show anchored above `anchor` (usually the
    // control-bar track chip) or hide if already open.
    void toggle(QWidget* anchor);
    bool isOpen() const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    enum class ChoiceKind {
        Off,
        Embedded,
        Addon,
        LocalFile,
    };

    struct Choice {
        ChoiceKind kind = ChoiceKind::Off;
        int     embeddedIndex = -1;
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
    void anchorAbove(QWidget* anchor);
    void installClickFilter();
    void removeClickFilter();

    static QString normalizeAddonKey(const tankostream::addon::SubtitleTrack& t);
    static QString addonDisplayLabel(const tankostream::addon::SubtitleTrack& t);
    static QString embeddedDisplayLabel(const SubtitleTrackInfo& t);

    SidecarProcess* m_sidecar = nullptr;

    QList<SubtitleTrackInfo> m_embeddedTracks;
    int  m_activeEmbeddedIndex = -1;

    QList<tankostream::addon::SubtitleTrack> m_addonTracks;
    QHash<QString, QString> m_addonOriginsByKey;

    QString m_customFilePath;
    QString m_activeChoiceKey;

    QList<Choice> m_choices;

    // Live-slider state, reapplied to URL-based choices on selection.
    int    m_delayMs  = 0;
    int    m_offsetPx = 0;
    double m_sizeScale = 1.0;

    QLabel*      m_titleLabel   = nullptr;
    QListWidget* m_choiceList   = nullptr;
    QPushButton* m_loadFileBtn  = nullptr;
    QSlider*     m_delaySlider  = nullptr;
    QLabel*      m_delayValue   = nullptr;
    QSlider*     m_offsetSlider = nullptr;
    QLabel*      m_offsetValue  = nullptr;
    QSlider*     m_sizeSlider   = nullptr;
    QLabel*      m_sizeValue    = nullptr;

    bool m_clickFilterInstalled = false;
    // Anchor tracked across toggle()/dismiss() — see FilterPopover note.
    QPointer<QWidget> m_anchor;
};
