#pragma once

#include <QDialog>
#include <QList>
#include <QSet>

#include "core/manga/MangaResult.h"

class QSpinBox;
class QLabel;
class QPushButton;

class ChapterRangeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChapterRangeDialog(const QList<ChapterInfo>& allChapters,
                                 const QSet<QString>& alreadyHandledIds,
                                 QWidget* parent = nullptr);

    // Filtered list of chapters in [from, to] that aren't already-handled.
    QList<ChapterInfo> selectedChapters() const;

private slots:
    void updatePreview();

private:
    QList<ChapterInfo> m_all;
    QSet<QString>      m_handled;

    QSpinBox* m_fromSpin    = nullptr;
    QSpinBox* m_toSpin      = nullptr;
    QLabel*   m_previewText = nullptr;
    QPushButton* m_dlBtn    = nullptr;
};
