// src/ui/pages/comics/ComicsSourceCard.h
#pragma once

#include "ComicsSourcesPanel.h"

#include <QFrame>

class QLabel;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QResizeEvent;

namespace tankoban::manga::comics {

// Stremio-style source card for the Comics sources sidebar. It mirrors the
// StreamSourceCard composition while rendering manga-native source rows.
class ComicsSourceCard : public QFrame
{
    Q_OBJECT
public:
    explicit ComicsSourceCard(const UnifiedSourceRow& row, QWidget* parent = nullptr);
    explicit ComicsSourceCard(bool skeleton, QWidget* parent = nullptr);
    ~ComicsSourceCard() override;

    const UnifiedSourceRow& row() const { return m_row; }
    bool isSkeleton() const { return m_skeleton; }

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    void clicked(const tankoban::manga::comics::UnifiedSourceRow& row);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUiRealRow();
    void buildUiSkeleton();
    void applyStateStyle();
    void reelideLabels();

    static QString badgeText(const UnifiedSourceRow& row);
    static QString tierPillText(const UnifiedSourceRow& row);
    static QString subtitleText(const UnifiedSourceRow& row);
    static QString archiveChipText(const UnifiedSourceRow& row);

    UnifiedSourceRow m_row;
    bool m_skeleton = false;
    bool m_hovered = false;
    bool m_selected = false;

    QLabel* m_badgeLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_tierPillLabel = nullptr;
};

} // namespace tankoban::manga::comics
