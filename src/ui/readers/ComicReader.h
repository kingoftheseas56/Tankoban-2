#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QPixmap>
#include <QTimer>

class ComicReader : public QWidget {
    Q_OBJECT
public:
    explicit ComicReader(QWidget* parent = nullptr);

    void openBook(const QString& cbzPath);

signals:
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void buildUI();
    void showPage(int index);
    void nextPage();
    void prevPage();
    void updatePageLabel();
    void displayCurrentPage();
    void prefetchNext();
    void showToolbar();
    void hideToolbar();

    // State
    QString     m_cbzPath;
    QStringList m_pageNames;
    int         m_currentPage = 0;
    QPixmap     m_currentPixmap;
    QPixmap     m_prefetched;
    int         m_prefetchedIndex = -1;

    // UI
    QLabel*      m_imageLabel = nullptr;
    QWidget*     m_toolbar    = nullptr;
    QPushButton* m_backBtn    = nullptr;
    QPushButton* m_prevBtn    = nullptr;
    QPushButton* m_nextBtn    = nullptr;
    QLabel*      m_pageLabel  = nullptr;
    QTimer       m_hideTimer;
};
