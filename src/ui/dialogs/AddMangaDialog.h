#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include "core/manga/MangaResult.h"

class AddMangaDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddMangaDialog(const QString& seriesTitle, const QString& source,
                            const QString& defaultDest, QWidget* parent = nullptr);

    void populateChapters(const QList<ChapterInfo>& chapters);
    void showError(const QString& message);

    // Selected chapters + config
    QList<ChapterInfo> selectedChapters() const;
    QString destinationPath() const;
    QString format() const;

private:
    void buildUI();
    void selectAll();
    void deselectAll();
    void selectRange();
    void updateStatus();

    QString m_seriesTitle;
    QString m_source;

    QLabel*       m_statusLabel  = nullptr;
    QTableWidget* m_chapterTable = nullptr;
    QLineEdit*    m_destEdit     = nullptr;
    QComboBox*    m_formatCombo  = nullptr;
    QSpinBox*     m_rangeFrom    = nullptr;
    QSpinBox*     m_rangeTo      = nullptr;
    QPushButton*  m_downloadBtn  = nullptr;

    QList<ChapterInfo> m_chapters;
};
