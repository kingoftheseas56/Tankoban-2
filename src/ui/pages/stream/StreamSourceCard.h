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

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void buildUI();
    void applyStateStyle();

    static QString addonInitials(const QString& addonName);

    StreamPickerChoice m_choice;
    bool m_hovered  = false;
    bool m_selected = false;
};

}
