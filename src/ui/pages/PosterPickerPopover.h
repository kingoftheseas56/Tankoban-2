#pragma once

#include <QFrame>
#include <QList>
#include <QString>
#include <QUrl>

#include "core/stream/addon/MetaItem.h"

class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QKeyEvent;

// Disambiguation picker for "Fetch poster from internet" when the title query
// returns multiple candidates (e.g. "The Boys" → 2019 TV show vs 2020 Danish
// film). Shown from VideosPage's context-menu handler when searchByTitle
// returns 2+ results.
//
// Pattern modelled on TrackPopover (src/ui/player/TrackPopover.h) — QFrame
// subclass, QListWidget-based, scoped QSS, keyboard-navigable. Uses
// `Qt::Popup` window flag for native outside-click dismiss rather than a
// manual application-level event filter.
//
// Thumbnails load async via PosterFetcher into a shared on-disk cache at
// $GenericDataLocation/Tankoban/data/poster_picker_thumbs/. List items show
// a plain placeholder until the thumb lands; rows remain clickable during
// download.
//
// One-shot lifecycle: deleteLater() on any dismissal path (selection, outside
// click, Escape). Caller does not retain a pointer.
class PosterPickerPopover : public QFrame
{
    Q_OBJECT

public:
    explicit PosterPickerPopover(QWidget* parent = nullptr);

    // Populate the list from `candidates` (capped at 5 rows) and show the
    // popover near `globalPos`, clamped to the current screen. Thumbnails
    // begin downloading immediately using `nam` (caller owns the QNAM).
    void showAtGlobal(const QList<tankostream::addon::MetaItemPreview>& candidates,
                      const QPoint& globalPos,
                      QNetworkAccessManager* nam);

signals:
    // Fired once when the user selects a candidate. Never fired when the
    // popover is dismissed without a choice.
    void posterChosen(QUrl posterUrl, QString metaName);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void dismiss();
    void loadThumb(int rowIndex, const QUrl& url, QNetworkAccessManager* nam);

    QListWidget*   m_list = nullptr;
    QLabel*        m_header = nullptr;
    QList<QUrl>    m_posterUrls;
    QList<QString> m_metaNames;
    QString        m_thumbCacheDir;
};
