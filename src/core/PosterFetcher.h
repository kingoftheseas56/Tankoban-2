#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Download-and-cache helper for poster images fetched from the internet.
// Copies the QNetworkAccessManager + 10s transfer-timeout + Mozilla UA
// pattern from CatalogBrowseScreen::ensurePoster (src/ui/pages/stream/
// CatalogBrowseScreen.cpp:340-379). Caller owns the QNAM — this keeps
// lifetime coupled to the page, not a hidden global.
//
// Usage:
//   PosterFetcher::download(m_nam, url, "/path/to/out.jpg", this,
//       [guard = QPointer<QWidget>(tile)](bool ok) {
//           if (ok && guard) guard->refresh();
//       });
//
// Callback fires on the receiver's thread. Failure cases that invoke cb(false):
// network error, empty body, disk write failure. Silent no-op if url is
// invalid or destPath is empty. Callback captures should QPointer-guard any
// widget they refer to — the download may outlive the widget.
class PosterFetcher {
public:
    static void download(QNetworkAccessManager* nam,
                         const QUrl& url,
                         const QString& destPath,
                         QObject* ctx,
                         std::function<void(bool success)> cb);
};
