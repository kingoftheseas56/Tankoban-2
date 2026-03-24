#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>

class CoreBridge;
class TileStrip;
class BooksScanner;
struct BookSeriesInfo;
struct AudiobookInfo;

class BooksPage : public QWidget {
    Q_OBJECT
public:
    explicit BooksPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~BooksPage();

    void activate();
    void triggerScan();

private slots:
    void onBookSeriesFound(const BookSeriesInfo& series);
    void onAudiobookFound(const AudiobookInfo& audiobook);
    void onScanFinished(const QList<BookSeriesInfo>& allBooks,
                        const QList<AudiobookInfo>& allAudiobooks);

private:
    void buildUI();

    CoreBridge*    m_bridge = nullptr;

    // Books section
    TileStrip*     m_bookStrip = nullptr;
    QLabel*        m_bookStatus = nullptr;

    // Audiobooks section
    QWidget*       m_audiobookSection = nullptr;
    QLabel*        m_audiobookTitle = nullptr;
    TileStrip*     m_audiobookStrip = nullptr;
    QLabel*        m_audiobookStatus = nullptr;

    // Scanner
    QThread*       m_scanThread = nullptr;
    BooksScanner*  m_scanner = nullptr;
    bool           m_hasScanned = false;
    bool           m_scanning = false;
};
