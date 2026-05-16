#pragma once

#include <QFrame>
#include <QLabel>
#include <QPixmap>

class QLineEdit;

class TileCard : public QFrame {
    Q_OBJECT
public:
    explicit TileCard(const QString& thumbPath,
                      const QString& title,
                      const QString& subtitle,
                      QWidget* parent = nullptr);

    void setCardSize(int width, int imageHeight);
    void setBadges(double progressFraction, const QString& pageBadge = {},
                   const QString& countBadge = {}, const QString& status = {});
    void setIsNew(bool isNew);
    void setIsFolder(bool isFolder);
    void setThumbPath(const QString& path);
    // STREAM_CATALOG_THUMBNAIL_PERSISTENCE 2026-05-06 — pre-decoded entry
    // point so callers with an in-memory pixmap cache can skip the
    // `QPixmap(path)` disk decode that setThumbPath performs every call.
    // Used by CatalogBrowseScreen's 3-layer poster cache (memory → disk →
    // network) for instant re-entry to the catalog board. Falling back to
    // the path remains via setThumbPath for callers that don't keep
    // raw pixmaps around.
    void setThumbPixmap(const QPixmap& pixmap);
    void setSelected(bool selected);
    void setFocused(bool focused);

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 33 — paint-time
    // provenance + downloading chips. setProvenance("tankoyomi") paints a
    // [Tankoyomi] chip at the top-left of the cover; setDownloadingChip(true)
    // paints a [DOWNLOADING] chip at the top-right. Both rerun applyBadges
    // when state changes; default state ("" / false) is a no-op for callers
    // who don't care (non-Comics pages).
    void setProvenance(const QString& provenance);
    void setDownloadingChip(bool show);

    // Inline title rename — shows a QLineEdit in place of the title label,
    // Enter/focus-out commits, Escape cancels. Emits renameCompleted.
    void beginRename();

    bool isSelected() const { return m_selected; }
    bool isFocused() const  { return m_focused; }
    int cardWidth() const   { return m_cardWidth; }
    int imageHeight() const { return m_imageHeight; }

    static constexpr int DEFAULT_WIDTH  = 200;
    static constexpr int DEFAULT_IMAGE_HEIGHT = 308;

signals:
    void clicked();
    void renameCompleted(bool accepted, const QString& newName);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void applyBadges();
    void updateBorder();
    void endRename(bool accepted);
    static QPixmap roundPixmap(const QPixmap& src, int radius);

    QFrame* m_imageWrap = nullptr;
    QLabel* m_imageLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLineEdit* m_renameEdit = nullptr;

    QString m_thumbPath;
    // STREAM_CATALOG_THUMBNAIL_PERSISTENCE 2026-05-06 — raw decoded pixmap
    // stash. setThumbPixmap populates it directly; setCardSize prefers
    // this over re-decoding from m_thumbPath. Cleared when setThumbPath
    // updates the path (path-side becomes authoritative again). m_basePixmap
    // is the post-scale/crop/round-render result (separate concept).
    QPixmap m_thumbPixmap;
    QString m_title;
    QString m_subtitle;
    QPixmap m_basePixmap;

    int m_cardWidth = DEFAULT_WIDTH;
    int m_imageHeight = DEFAULT_IMAGE_HEIGHT;

    double  m_progressFraction = 0.0;
    QString m_pageBadge;
    QString m_countBadge;
    QString m_status;
    bool    m_isNew    = false;
    bool    m_isFolder = false;
    bool    m_selected = false;
    bool    m_focused  = false;
    bool    m_hovered  = false;
    bool    m_flashing = false;
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 33 — provenance
    // + downloading chip state. Painted in applyBadges; default-empty means
    // no extra paint cost on non-Comics pages.
    QString m_provenance;
    bool    m_downloadingChip = false;
};
