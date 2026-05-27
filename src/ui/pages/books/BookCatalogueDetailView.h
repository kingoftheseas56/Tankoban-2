#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "core/book/BookCatalogueResult.h"
#include "core/book/BookResult.h"

class BookScraper;
class BookSearchAggregator;
class BooksCatalogueLibraryStore;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class TankorentSearchService;

class BookCatalogueDetailView : public QWidget
{
    Q_OBJECT

public:
    explicit BookCatalogueDetailView(QWidget* parent = nullptr);

    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    void showBook(const BookCatalogueResult& book, const QString& coverPath);

signals:
    void backRequested();

private:
    struct SourceSection {
        QWidget* container = nullptr;
        QLabel* heading = nullptr;
        QWidget* rows = nullptr;
        int resultCount = 0;
    };

    void buildUi();
    void resetSourceSections();
    void startSourceSearch();
    void recreateSourceAggregator(int generation);
    void renderSourceRow(SourceSection& section, const BookResult& result);
    void renderSourceMessage(SourceSection& section, const QString& sourceId,
                             const QString& message);
    void clearRows(QWidget* rows);
    void setCover(const QString& coverPath);

    QPushButton* m_backButton = nullptr;
    QLabel* m_coverLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_authorLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QLabel* m_sourceStatus = nullptr;

    QHash<QString, SourceSection> m_sourceSections;

    QNetworkAccessManager* m_nam = nullptr;
    TankorentSearchService* m_tankorentService = nullptr;
    BookSearchAggregator* m_sourceAggregator = nullptr;
    QList<BookScraper*> m_sourceScrapers;

    BooksCatalogueLibraryStore* m_catalogueStore = nullptr;
    BookCatalogueResult m_currentBook;
    QString m_currentCatalogueId;
    QString m_currentCoverPath;
    int m_generation = 0;
};
