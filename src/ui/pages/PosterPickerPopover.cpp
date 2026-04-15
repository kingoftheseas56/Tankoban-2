#include "PosterPickerPopover.h"

#include "core/PosterFetcher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QPixmap>
#include <QRect>
#include <QScreen>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

// Match plan: 60x90 thumb + title + year text in a vertical list, 5 rows cap.
constexpr int kThumbW = 60;
constexpr int kThumbH = 90;
constexpr int kMaxRows = 5;
constexpr int kPopoverW = 320;

QPixmap makePlaceholderThumb()
{
    QPixmap pm(kThumbW, kThumbH);
    pm.fill(QColor(40, 40, 40));
    return pm;
}

}

PosterPickerPopover::PosterPickerPopover(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName("PosterPickerPopover");
    setAttribute(Qt::WA_DeleteOnClose, false);  // we control delete via deleteLater in dismiss()
    setFocusPolicy(Qt::StrongFocus);

    // Scoped QSS — grayscale chrome only (feedback_no_color_no_emoji). Color
    // in the rows comes from fetched poster thumbnails, which are media
    // content, not UI chrome.
    setStyleSheet(
        "#PosterPickerPopover {"
        "  background: rgba(16,16,16,240);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 8px;"
        "}"
        "#PosterPickerPopover QLabel#PosterPickerHeader {"
        "  color: rgba(214,194,164,0.95);"
        "  font-size: 11px;"
        "  font-weight: 700;"
        "  padding-bottom: 4px;"
        "  border: none;"
        "}"
        "#PosterPickerPopover QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "  color: rgba(255,255,255,0.92);"
        "  font-size: 12px;"
        "}"
        "#PosterPickerPopover QListWidget::item {"
        "  padding: 6px;"
        "  border-radius: 4px;"
        "}"
        "#PosterPickerPopover QListWidget::item:hover {"
        "  background: rgba(255,255,255,0.08);"
        "}"
        "#PosterPickerPopover QListWidget::item:selected {"
        "  background: rgba(255,255,255,0.12);"
        "}"
    );
    setFixedWidth(kPopoverW);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(4);

    m_header = new QLabel("Choose a poster");
    m_header->setObjectName("PosterPickerHeader");
    lay->addWidget(m_header);

    m_list = new QListWidget;
    m_list->setIconSize(QSize(kThumbW, kThumbH));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setUniformItemSizes(true);
    lay->addWidget(m_list);

    auto emitChoice = [this](QListWidgetItem* item) {
        if (!item) return;
        const int row = m_list->row(item);
        if (row < 0 || row >= m_posterUrls.size()) return;
        emit posterChosen(m_posterUrls.at(row), m_metaNames.at(row));
        dismiss();
    };
    connect(m_list, &QListWidget::itemClicked, this, emitChoice);
    connect(m_list, &QListWidget::itemActivated, this, emitChoice);

    m_thumbCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                      + QStringLiteral("/Tankoban/data/poster_picker_thumbs");
    QDir().mkpath(m_thumbCacheDir);
}

void PosterPickerPopover::showAtGlobal(
    const QList<tankostream::addon::MetaItemPreview>& candidates,
    const QPoint& globalPos,
    QNetworkAccessManager* nam)
{
    m_list->clear();
    m_posterUrls.clear();
    m_metaNames.clear();

    const int cap = qMin<int>(candidates.size(), kMaxRows);
    const QIcon placeholder(makePlaceholderThumb());

    for (int i = 0; i < cap; ++i) {
        const auto& c = candidates.at(i);
        QString text = c.name;
        if (!c.releaseInfo.isEmpty())
            text += QStringLiteral(" (") + c.releaseInfo + QLatin1Char(')');

        auto* item = new QListWidgetItem(placeholder, text);
        m_list->addItem(item);

        m_posterUrls.append(c.poster);
        m_metaNames.append(c.name);

        if (c.poster.isValid() && nam)
            loadThumb(i, c.poster, nam);
    }

    // Fixed height based on row count so the list doesn't over-expand when
    // only two candidates exist. 100 px per row accommodates the 90 px thumb
    // plus list item padding.
    const int rowHeight = 100;
    setFixedHeight(40 + cap * rowHeight + 10);

    m_list->setCurrentRow(0);

    // Position below-right of the cursor, clamped to the screen the cursor
    // is on so the popover never lands off-screen.
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    int x = qMin(globalPos.x(), avail.right() - width());
    int y = qMin(globalPos.y(), avail.bottom() - height());
    x = qMax(x, avail.left());
    y = qMax(y, avail.top());

    move(x, y);
    show();
    raise();
    activateWindow();
    m_list->setFocus();
}

void PosterPickerPopover::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    QFrame::keyPressEvent(event);
}

void PosterPickerPopover::dismiss()
{
    hide();
    deleteLater();
}

void PosterPickerPopover::loadThumb(int rowIndex, const QUrl& url, QNetworkAccessManager* nam)
{
    const QString urlHash = QString::fromLatin1(
        QCryptographicHash::hash(url.toString().toUtf8(),
                                 QCryptographicHash::Sha1).toHex().left(20));
    const QString path = m_thumbCacheDir + QLatin1Char('/') + urlHash + QStringLiteral(".jpg");

    auto applyThumb = [this, rowIndex, path]() {
        QPixmap pm(path);
        if (pm.isNull()) return;
        if (rowIndex < 0 || rowIndex >= m_list->count()) return;
        m_list->item(rowIndex)->setIcon(QIcon(
            pm.scaled(kThumbW, kThumbH,
                      Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };

    if (QFile::exists(path)) {
        QPixmap pm(path);
        if (!pm.isNull()) {
            applyThumb();
            return;
        }
        // Cached file is corrupt (pre-validation build, aborted write, bad
        // HTML response written as .jpg). Remove and fall through to a
        // fresh download.
        QFile::remove(path);
    }

    QPointer<PosterPickerPopover> self(this);
    PosterFetcher::download(nam, url, path, this,
        [self, applyThumb](bool ok) {
            if (!self) return;
            if (ok) applyThumb();
        });
}
