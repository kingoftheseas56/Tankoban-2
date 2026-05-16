#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task C2) - pack-list row widget.
// 3-line vertical layout: bold title (13px, single line, elide-right) +
// chip row (10px, 4px chip radius, grayscale borders) + meta line
// (11px, 48% opacity). Reuses StreamSourceCard hierarchy per Codex
// expansion 5.3.A in the brainstorm-md.

#include "core/stream/UnifiedPackSearchEngine.h"

#include <QFrame>

class QLabel;
class QHBoxLayout;
class QMouseEvent;
class QResizeEvent;

namespace tankoban::stream::theatre {

class PackListItem : public QFrame {
    Q_OBJECT
public:
    explicit PackListItem(const EnrichedPack& pack, QWidget* parent = nullptr);

    void setSelected(bool selected);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUI();
    void relayoutTitle();
    QLabel* makeChip(const QString& text, const QString& objectName);

    EnrichedPack m_pack;
    bool         m_selected = false;

    QLabel*      m_titleLabel = nullptr;
    QHBoxLayout* m_chipRow    = nullptr;
    QLabel*      m_metaLabel  = nullptr;
};

}  // namespace tankoban::stream::theatre
