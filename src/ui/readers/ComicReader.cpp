#include "ComicReader.h"
#include "core/ArchiveReader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>

ComicReader::ComicReader(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #000000;");

    buildUI();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &ComicReader::hideToolbar);
}

void ComicReader::buildUI()
{
    // Page image (fills the widget)
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background: transparent;");

    // Bottom toolbar
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName("ComicReaderToolbar");
    m_toolbar->setFixedHeight(48);
    m_toolbar->setStyleSheet(
        "QWidget#ComicReaderToolbar {"
        "  background: rgba(8, 8, 8, 0.82);"
        "  border-top: 1px solid rgba(255, 255, 255, 0.10);"
        "}"
    );

    auto* tbLayout = new QHBoxLayout(m_toolbar);
    tbLayout->setContentsMargins(16, 0, 16, 0);
    tbLayout->setSpacing(12);

    m_backBtn = new QPushButton(QChar(0x2190) + QString(" Back"), m_toolbar);
    m_backBtn->setObjectName("IconButton");
    m_backBtn->setFixedHeight(28);
    m_backBtn->setMinimumWidth(60);
    m_backBtn->setMaximumWidth(80);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "QPushButton { color: rgba(255,255,255,0.78); background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.10); border-radius: 8px;"
        "  padding: 4px 10px; font-size: 11px; min-width: 60px; max-width: 80px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.12); }"
    );
    connect(m_backBtn, &QPushButton::clicked, this, &ComicReader::closeRequested);
    tbLayout->addWidget(m_backBtn);

    tbLayout->addStretch();

    m_prevBtn = new QPushButton(QChar(0x25C1), m_toolbar);
    m_prevBtn->setObjectName("IconButton");
    m_prevBtn->setFixedSize(32, 28);
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &ComicReader::prevPage);
    tbLayout->addWidget(m_prevBtn);

    m_pageLabel = new QLabel("Page 1 / 1", m_toolbar);
    m_pageLabel->setStyleSheet("color: rgba(255,255,255,0.78); font-size: 12px; background: transparent;");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setMinimumWidth(100);
    tbLayout->addWidget(m_pageLabel);

    m_nextBtn = new QPushButton(QChar(0x25B7), m_toolbar);
    m_nextBtn->setObjectName("IconButton");
    m_nextBtn->setFixedSize(32, 28);
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &ComicReader::nextPage);
    tbLayout->addWidget(m_nextBtn);

    tbLayout->addStretch();

    // Spacer to balance the back button
    auto* spacer = new QWidget(m_toolbar);
    spacer->setFixedWidth(80);
    spacer->setStyleSheet("background: transparent;");
    tbLayout->addWidget(spacer);
}

void ComicReader::openBook(const QString& cbzPath)
{
    m_cbzPath = cbzPath;
    m_pageNames = ArchiveReader::pageList(cbzPath);
    m_currentPage = 0;
    m_prefetchedIndex = -1;
    m_prefetched = QPixmap();

    if (m_pageNames.isEmpty()) {
        m_imageLabel->setText("No pages found in this archive");
        m_imageLabel->setStyleSheet("color: rgba(255,255,255,0.58); font-size: 14px; background: transparent;");
        updatePageLabel();
        return;
    }

    showPage(0);
    showToolbar();
}

void ComicReader::showPage(int index)
{
    if (m_pageNames.isEmpty()) return;
    index = qBound(0, index, m_pageNames.size() - 1);

    if (index == m_prefetchedIndex && !m_prefetched.isNull()) {
        m_currentPixmap = m_prefetched;
    } else {
        QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageNames[index]);
        m_currentPixmap.loadFromData(data);
    }

    m_currentPage = index;
    displayCurrentPage();
    updatePageLabel();
    prefetchNext();
}

void ComicReader::displayCurrentPage()
{
    if (m_currentPixmap.isNull()) return;

    int availW = width();
    int availH = height() - (m_toolbar->isVisible() ? m_toolbar->height() : 0);
    if (availW <= 0 || availH <= 0) return;

    QPixmap scaled = m_currentPixmap.scaled(availW, availH,
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
    m_imageLabel->setGeometry(0, 0, width(), height());
}

void ComicReader::prefetchNext()
{
    int nextIdx = m_currentPage + 1;
    if (nextIdx < m_pageNames.size() && nextIdx != m_prefetchedIndex) {
        QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageNames[nextIdx]);
        m_prefetched.loadFromData(data);
        m_prefetchedIndex = nextIdx;
    }
}

void ComicReader::nextPage()
{
    if (m_currentPage < m_pageNames.size() - 1)
        showPage(m_currentPage + 1);
}

void ComicReader::prevPage()
{
    if (m_currentPage > 0)
        showPage(m_currentPage - 1);
}

void ComicReader::updatePageLabel()
{
    if (m_pageNames.isEmpty()) {
        m_pageLabel->setText("No pages");
    } else {
        m_pageLabel->setText(QString("Page %1 / %2")
                            .arg(m_currentPage + 1)
                            .arg(m_pageNames.size()));
    }
}

void ComicReader::showToolbar()
{
    m_toolbar->show();
    m_toolbar->raise();
    m_hideTimer.start();
}

void ComicReader::hideToolbar()
{
    m_toolbar->hide();
}

void ComicReader::keyPressEvent(QKeyEvent* event)
{
    showToolbar();

    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_Space:
    case Qt::Key_PageDown:
        nextPage();
        break;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
        prevPage();
        break;
    case Qt::Key_Home:
        showPage(0);
        break;
    case Qt::Key_End:
        showPage(m_pageNames.size() - 1);
        break;
    case Qt::Key_Escape:
        emit closeRequested();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ComicReader::wheelEvent(QWheelEvent* event)
{
    showToolbar();
    if (event->angleDelta().y() < 0)
        nextPage();
    else if (event->angleDelta().y() > 0)
        prevPage();
}

void ComicReader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Position toolbar at bottom
    m_toolbar->setGeometry(0, height() - m_toolbar->height(),
                           width(), m_toolbar->height());

    // Re-display page at new size
    displayCurrentPage();
}

void ComicReader::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showToolbar();
}
