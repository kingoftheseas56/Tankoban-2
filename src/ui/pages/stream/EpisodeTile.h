#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — scope-picker episode tile.
// Compact row: S·E label + title (if known) + size + checkbox + optional
// "Have" badge for already-downloaded episodes. Per Codex expansion
// 5.5.A + 5.5.B.

#include <QFrame>

class QCheckBox;
class QLabel;

namespace tankoban::stream::theatre {

struct EpisodeTileData {
    int     season = 0;
    int     episode = 0;
    QString title;
    qint64  sizeBytes = 0;
    bool    alreadyHave = false;
};

class EpisodeTile : public QFrame {
    Q_OBJECT
public:
    explicit EpisodeTile(const EpisodeTileData& data, QWidget* parent = nullptr);

    bool isChecked() const;
    void setChecked(bool checked);

    int season() const  { return m_data.season; }
    int episode() const { return m_data.episode; }

signals:
    void toggled(bool checked);

private:
    void buildUI();

    EpisodeTileData m_data;
    QCheckBox*      m_checkBox = nullptr;
    QLabel*         m_seLabel  = nullptr;
    QLabel*         m_titleLabel = nullptr;
    QLabel*         m_sizeLabel  = nullptr;
    QLabel*         m_haveBadge  = nullptr;
};

}  // namespace tankoban::stream::theatre
