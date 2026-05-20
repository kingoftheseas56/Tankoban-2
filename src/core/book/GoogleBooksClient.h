#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include "BookCatalogueResult.h"

class QNetworkAccessManager;

// HTTP client + JSON parser for Google Books (https://www.googleapis.com/books/v1).
// Fallback catalogue source — broader catalog than Open Library for newer
// releases + non-English titles; requires API key (TANKOBAN_GOOGLE_BOOKS_KEY
// env var, set in build_and_run.bat per writing-plans coordination).
//
// API endpoints used by v1:
//   - GET /volumes?q=<query>&key=<KEY>   -> volume search
class GoogleBooksClient : public QObject
{
    Q_OBJECT

public:
    explicit GoogleBooksClient(QNetworkAccessManager* nam,
                               const QString& apiKey,
                               QObject* parent = nullptr);

    static QList<BookCatalogueResult> parseVolumesResponse(const QByteArray& json);

    void search(const QString& query);

signals:
    void searchResults(const QList<BookCatalogueResult>& results);
    void searchFailed(const QString& error);

private:
    void onSearchReply();

    QNetworkAccessManager* m_nam;
    QString m_apiKey;
};
