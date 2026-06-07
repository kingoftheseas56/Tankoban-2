// src/ui/pages/comics/ComicsSourceCard.h
#pragma once

// Restyled per docs/superpowers/specs/2026-05-20-comics-series-page-polish-design.md
// section 3.7. Card surface now exposes the spec setter API (release title,
// host name + type, uploader, size, seed count, trusted/fallback badging,
// download button with "Download Vol N" label). The legacy
// ComicsSourcesPanel constructors (UnifiedSourceRow, bool skeleton) are
// preserved verbatim so the panel keeps populating without churn while
// Task 5 (ComicsSourcesPanel context line + caller migration) ships.
//
// New callers should prefer the setter-based API. The UnifiedSourceRow ctor
// internally maps row.kind -> HostType, row.uploaderHint -> uploader, etc.,
// then drives the same rebuildMetaLine() path the spec describes.

#include "ComicsSourcesPanel.h"

#include <QFrame>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QResizeEvent;

namespace tankoban::manga::comics {

// Stremio-style source card for the Comics sources sidebar (post-restyle).
// Visual contract: docs/superpowers/specs/2026-05-20-comics-series-page-polish-design.md
// section 3.7 (release title / host / meta line / download button +
// trusted+fallback badging + fallback styling).
class ComicsSourceCard : public QFrame
{
    Q_OBJECT
public:
    // Host type for visual styling. Drives host-label color (purple for
    // recognised hosts, muted gray for fallback) and any per-host nuances.
    enum class HostType { Nyaa, WeebCentral, TankoyomiSource, Other };

    // Legacy ctor still used by ComicsSourcesPanel.cpp.
    explicit ComicsSourceCard(const UnifiedSourceRow& row, QWidget* parent = nullptr);
    explicit ComicsSourceCard(bool skeleton, QWidget* parent = nullptr);
    ~ComicsSourceCard() override;

    const UnifiedSourceRow& row() const { return m_row; }
    bool isSkeleton() const { return m_skeleton; }

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    // --- Spec §3.7 setter API (preferred for new callers) ---
    // All setters trigger a rebuildMetaLine() + applyStylePerType() so the
    // card re-renders with the latest snapshot. Cheap; safe to drive from
    // any thread that already lives on the GUI thread.
    void setReleaseTitle(const QString& title);
    void setHostName(const QString& host);
    void setHostType(HostType type);
    void setUploader(const QString& uploader);
    void setSizeBytes(qint64 size);
    void setSeedCount(int seeds);          // -1 = N/A (e.g. WeebCentral)
    void setIsFallback(bool fallback);
    void setVolumeNumber(int volumeN);     // drives "Download <unit> N" label
    void setUnitWord(const QString& unit); // unit word for the download button ("Vol" default / "Issue")

signals:
    void clicked(const tankoban::manga::comics::UnifiedSourceRow& row);
    // Emitted when the user clicks the dedicated download button on the
    // card (spec §3.7 action row). Distinct from clicked() which fires on
    // any mouse-release inside the card body (legacy panel selection
    // behaviour).
    void downloadClicked(const tankoban::manga::comics::UnifiedSourceRow& row);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // New (spec §3.7) builders.
    void buildUi();
    void buildUiSkeleton();
    void rebuildMetaLine();
    void applyStylePerType();
    void rebuildDownloadButtonLabel();
    void reelideTitle();

    // Drives setHostType + isFallback defaults from UnifiedSourceRow.kind.
    void seedFromUnifiedRow(const UnifiedSourceRow& row);

    UnifiedSourceRow m_row;
    bool m_skeleton = false;
    bool m_hovered = false;
    bool m_selected = false;

    // Spec §3.7 state.
    HostType m_hostType    = HostType::Other;
    bool     m_isFallback  = false;
    QString  m_releaseTitle;
    QString  m_hostName;
    QString  m_uploader;
    qint64   m_sizeBytes   = 0;
    int      m_seedCount   = -1;
    int      m_volumeNumber = 0;
    QString  m_unitWord     = QStringLiteral("Vol");  // download-button unit; "Issue" for western

    // Widgets owned by Qt parent chain.
    QLabel*       m_titleLabel     = nullptr;
    QLabel*       m_metaLabel      = nullptr;
    QPushButton*  m_downloadButton = nullptr;
};

} // namespace tankoban::manga::comics
