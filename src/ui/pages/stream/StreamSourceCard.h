#pragma once

#include <QFrame>

#include "StreamSourceChoice.h"

class QLabel;

namespace tankostream::stream {

// Stream-picker UX rework — replaces one row of the old StreamPickerDialog
// QTableWidget with a Stremio-style card: addon-initials badge (left), two-
// line text column (addon display name + filename), quality pill (right),
// and a bottom chip row with peer count, size, and any HDR/DV/sub badges.
//
// Fires `clicked(choice)` on a single left-click anywhere within the card.
// Hover surfaces a subtle highlight; `setSelected(true)` draws a persistent
// highlight (used for the user's saved choice on a return visit).
class StreamSourceCard : public QFrame
{
    Q_OBJECT

public:
    explicit StreamSourceCard(const StreamPickerChoice& choice, QWidget* parent = nullptr);

    const StreamPickerChoice& choice() const { return m_choice; }
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    void clicked(const tankostream::stream::StreamPickerChoice& choice);

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — emitted on right-click of a
    // magnet card. Non-magnet cards (HTTP/URL/youtube direct streams)
    // suppress the menu entirely so the affordance never appears for
    // sources Tankorent can't act on.
    void addToTankorentRequested(const tankostream::stream::StreamPickerChoice& choice);

    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - direct dispatch of
    // the right-clicked stream via TorrentClient::startDownload (Theatre
    // library route). Contrast with addToTankorentRequested which routes the
    // stream into the Tankorent-tab download manager. Parallel signal chain.
    void directDownloadRequested(const tankostream::stream::StreamPickerChoice& choice);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — re-elide the primary
    // release-name label on width change so long titles stay
    // visually-truncated with "…" rather than visibly clipped at the
    // pixel boundary. Tooltip carries the full untruncated string.
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUI();
    void applyStateStyle();
    void reelideTitle();

    static QString addonInitials(const QString& addonName);

    StreamPickerChoice m_choice;
    QLabel*            m_titleLabel = nullptr;
    bool m_hovered  = false;
    bool m_selected = false;
};

}
