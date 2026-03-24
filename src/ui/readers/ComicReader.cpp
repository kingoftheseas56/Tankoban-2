#include "ComicReader.h"
#include "SmoothScrollArea.h"
#include "DecodeTask.h"
#include "core/ArchiveReader.h"
#include "core/CoreBridge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QScrollBar>
#include <QCryptographicHash>
#include <QJsonObject>

static constexpr double SPREAD_RATIO = 1.08;

ComicReader::ComicReader(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #000000;");

    m_decodePool.setMaxThreadCount(2);

    buildUI();

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &ComicReader::hideToolbar);

    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(1200);
}

void ComicReader::buildUI()
{
    // Smooth scroll area for page display
    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("background: transparent; border: none;");
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel();
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background: transparent;");
    m_scrollArea->setWidget(m_imageLabel);

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
    tbLayout->setSpacing(8);

    auto makeBtn = [this](const QString& text, int w = 32) {
        auto* btn = new QPushButton(text, m_toolbar);
        btn->setFixedSize(w, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { color: rgba(255,255,255,0.78); background: rgba(255,255,255,0.06);"
            "  border: 1px solid rgba(255,255,255,0.10); border-radius: 8px;"
            "  padding: 0 8px; font-size: 11px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.12); }"
        );
        return btn;
    };

    m_backBtn = makeBtn(QChar(0x2190) + QString(" Back"), 70);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        saveCurrentProgress();
        emit closeRequested();
    });
    tbLayout->addWidget(m_backBtn);

    m_prevVolBtn = makeBtn(QString(QChar(0x00AB)), 32);
    m_prevVolBtn->setToolTip("Previous volume");
    m_prevVolBtn->hide();
    connect(m_prevVolBtn, &QPushButton::clicked, this, &ComicReader::prevVolume);
    tbLayout->addWidget(m_prevVolBtn);

    tbLayout->addStretch();

    m_prevBtn = makeBtn(QString(QChar(0x25C1)), 32);
    connect(m_prevBtn, &QPushButton::clicked, this, &ComicReader::prevPage);
    tbLayout->addWidget(m_prevBtn);

    m_pageLabel = new QLabel("Page 1 / 1", m_toolbar);
    m_pageLabel->setStyleSheet("color: rgba(255,255,255,0.78); font-size: 12px; background: transparent;");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setMinimumWidth(120);
    tbLayout->addWidget(m_pageLabel);

    m_nextBtn = makeBtn(QString(QChar(0x25B7)), 32);
    connect(m_nextBtn, &QPushButton::clicked, this, &ComicReader::nextPage);
    tbLayout->addWidget(m_nextBtn);

    tbLayout->addStretch();

    m_nextVolBtn = makeBtn(QString(QChar(0x00BB)), 32);
    m_nextVolBtn->setToolTip("Next volume");
    m_nextVolBtn->hide();
    connect(m_nextVolBtn, &QPushButton::clicked, this, &ComicReader::nextVolume);
    tbLayout->addWidget(m_nextVolBtn);

    auto* spacer = new QWidget(m_toolbar);
    spacer->setFixedWidth(70);
    spacer->setStyleSheet("background: transparent;");
    tbLayout->addWidget(spacer);
}

// ── Open / Page Display ─────────────────────────────────────────────────────

void ComicReader::openBook(const QString& cbzPath,
                            const QStringList& seriesCbzList,
                            const QString& seriesName)
{
    m_cbzPath = cbzPath;
    m_seriesCbzList = seriesCbzList;
    m_seriesName = seriesName;
    m_pageNames = ArchiveReader::pageList(cbzPath);
    m_currentPage = 0;
    m_currentPixmap = QPixmap();
    m_secondPixmap = QPixmap();
    m_cache.clear();

    // Build page metadata
    m_pageMeta.clear();
    m_pageMeta.resize(m_pageNames.size());
    for (int i = 0; i < m_pageNames.size(); ++i) {
        m_pageMeta[i].index = i;
        m_pageMeta[i].filename = m_pageNames[i];
    }

    if (m_pageNames.isEmpty()) {
        m_imageLabel->setText("No pages found in this archive");
        m_imageLabel->setStyleSheet("color: rgba(255,255,255,0.58); font-size: 14px; background: transparent;");
        updatePageLabel();
        return;
    }

    // Volume nav visibility
    int volIdx = m_seriesCbzList.indexOf(m_cbzPath);
    m_prevVolBtn->setVisible(!m_seriesCbzList.isEmpty() && volIdx > 0);
    m_nextVolBtn->setVisible(!m_seriesCbzList.isEmpty() && volIdx < m_seriesCbzList.size() - 1);

    int startPage = restoreSavedPage();
    showPage(startPage);
    showToolbar();
}

void ComicReader::requestDecode(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pageNames.size())
        return;
    if (m_cache.contains(pageIndex))
        return;

    auto* task = new DecodeTask(pageIndex, m_cbzPath, m_pageNames[pageIndex]);
    connect(&task->notifier, &DecodeTaskSignals::decoded,
            this, &ComicReader::onPageDecoded, Qt::QueuedConnection);
    m_decodePool.start(task);
}

void ComicReader::onPageDecoded(int pageIndex, const QPixmap& pixmap, int w, int h)
{
    m_cache.insert(pageIndex, pixmap);

    // Update metadata
    if (pageIndex >= 0 && pageIndex < m_pageMeta.size()) {
        m_pageMeta[pageIndex].width = w;
        m_pageMeta[pageIndex].height = h;
        m_pageMeta[pageIndex].isSpread = (h > 0 && static_cast<double>(w) / h > SPREAD_RATIO);
        m_pageMeta[pageIndex].decoded = true;
    }

    // If this is the current page (or second page in double mode), redisplay
    if (pageIndex == m_currentPage) {
        m_currentPixmap = pixmap;
        displayCurrentPage();
    }
    if (m_doublePageMode && pageIndex == m_currentPage + 1) {
        if (!isSpreadIndex(m_currentPage) && m_currentPage > 0) {
            m_secondPixmap = pixmap;
            displayCurrentPage();
        }
    }
}

bool ComicReader::isSpreadIndex(int index) const
{
    if (index < 0 || index >= m_pageMeta.size()) return false;
    return m_pageMeta[index].isSpread;
}

int ComicReader::pageAdvanceCount() const
{
    if (!m_doublePageMode) return 1;
    if (m_currentPage == 0) return 1; // cover always alone
    if (isSpreadIndex(m_currentPage)) return 1;
    if (!m_secondPixmap.isNull()) return 2;
    return 1;
}

QPixmap ComicReader::compositeDoublePages(const QPixmap& left, const QPixmap& right)
{
    int h = qMax(left.height(), right.height());
    int w = left.width() + right.width() + 4;
    QPixmap combined(w, h);
    combined.fill(Qt::black);
    QPainter painter(&combined);
    painter.drawPixmap(0, (h - left.height()) / 2, left);
    painter.drawPixmap(left.width() + 4, (h - right.height()) / 2, right);
    return combined;
}

void ComicReader::showPage(int index)
{
    if (m_pageNames.isEmpty()) return;
    index = qBound(0, index, m_pageNames.size() - 1);
    m_currentPage = index;
    m_secondPixmap = QPixmap();

    // Pin current page in cache
    m_cache.unpin(index - 2);
    m_cache.unpin(index - 1);
    m_cache.pin(index);

    // Try to get from cache
    if (m_cache.contains(index)) {
        m_currentPixmap = m_cache.get(index);
        // Update spread info from metadata
        if (index < m_pageMeta.size() && !m_pageMeta[index].decoded) {
            m_pageMeta[index].width = m_currentPixmap.width();
            m_pageMeta[index].height = m_currentPixmap.height();
            m_pageMeta[index].isSpread = (m_currentPixmap.height() > 0 &&
                static_cast<double>(m_currentPixmap.width()) / m_currentPixmap.height() > SPREAD_RATIO);
            m_pageMeta[index].decoded = true;
        }
    } else {
        // Decode synchronously for immediate display, then cache
        QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageNames[index]);
        m_currentPixmap.loadFromData(data);
        if (!m_currentPixmap.isNull()) {
            m_cache.insert(index, m_currentPixmap);
            if (index < m_pageMeta.size()) {
                m_pageMeta[index].width = m_currentPixmap.width();
                m_pageMeta[index].height = m_currentPixmap.height();
                m_pageMeta[index].isSpread = (m_currentPixmap.height() > 0 &&
                    static_cast<double>(m_currentPixmap.width()) / m_currentPixmap.height() > SPREAD_RATIO);
                m_pageMeta[index].decoded = true;
            }
        }
    }

    // Double-page: load second page
    if (m_doublePageMode && index > 0 && !isSpreadIndex(index)) {
        int nextIdx = index + 1;
        if (nextIdx < m_pageNames.size()) {
            if (m_cache.contains(nextIdx)) {
                QPixmap nextPix = m_cache.get(nextIdx);
                if (!isSpreadIndex(nextIdx))
                    m_secondPixmap = nextPix;
            } else {
                QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageNames[nextIdx]);
                QPixmap nextPix;
                nextPix.loadFromData(data);
                if (!nextPix.isNull()) {
                    m_cache.insert(nextIdx, nextPix);
                    if (nextIdx < m_pageMeta.size()) {
                        m_pageMeta[nextIdx].width = nextPix.width();
                        m_pageMeta[nextIdx].height = nextPix.height();
                        m_pageMeta[nextIdx].isSpread = (nextPix.height() > 0 &&
                            static_cast<double>(nextPix.width()) / nextPix.height() > SPREAD_RATIO);
                        m_pageMeta[nextIdx].decoded = true;
                    }
                    if (!isSpreadIndex(nextIdx))
                        m_secondPixmap = nextPix;
                }
            }
            m_cache.pin(nextIdx);
        }
    }

    displayCurrentPage();
    updatePageLabel();
    prefetchNeighbors();
    saveCurrentProgress();
}

void ComicReader::displayCurrentPage()
{
    if (m_currentPixmap.isNull()) return;

    int availW = m_scrollArea->viewport()->width();
    int availH = m_scrollArea->viewport()->height();
    if (availW <= 0 || availH <= 0) return;

    QPixmap composite = m_currentPixmap;
    if (m_doublePageMode && !m_secondPixmap.isNull())
        composite = compositeDoublePages(m_currentPixmap, m_secondPixmap);

    QPixmap scaled;
    switch (m_fitMode) {
    case FitMode::FitPage:
        scaled = composite.scaled(availW, availH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        break;
    case FitMode::FitWidth:
        scaled = composite.scaledToWidth(availW, Qt::SmoothTransformation);
        break;
    case FitMode::FitHeight:
        scaled = composite.scaledToHeight(availH, Qt::SmoothTransformation);
        break;
    }

    m_imageLabel->setPixmap(scaled);
    m_imageLabel->resize(scaled.size());

    if (auto* vbar = m_scrollArea->verticalScrollBar())
        vbar->setValue(0);
}

void ComicReader::prefetchNeighbors()
{
    int advance = pageAdvanceCount();
    // Prefetch next 2-4 pages async
    for (int i = 1; i <= 4; ++i) {
        int idx = m_currentPage + advance + i - 1;
        if (idx < m_pageNames.size())
            requestDecode(idx);
    }
    // Prefetch previous page
    if (m_currentPage > 0)
        requestDecode(m_currentPage - 1);
}

void ComicReader::nextPage()
{
    int advance = pageAdvanceCount();
    int next = m_currentPage + advance;
    if (next < m_pageNames.size())
        showPage(next);
}

void ComicReader::prevPage()
{
    if (m_currentPage <= 0) return;
    if (!m_doublePageMode) {
        showPage(m_currentPage - 1);
        return;
    }
    int target = qMax(0, m_currentPage - 2);
    showPage(target);
}

void ComicReader::updatePageLabel()
{
    if (m_pageNames.isEmpty()) {
        m_pageLabel->setText("No pages");
        return;
    }
    if (m_doublePageMode && !m_secondPixmap.isNull()) {
        m_pageLabel->setText(QString("Pages %1-%2 / %3")
            .arg(m_currentPage + 1)
            .arg(m_currentPage + 2)
            .arg(m_pageNames.size()));
    } else {
        m_pageLabel->setText(QString("Page %1 / %2")
            .arg(m_currentPage + 1)
            .arg(m_pageNames.size()));
    }
}

// ── Progress Persistence ────────────────────────────────────────────────────

QString ComicReader::itemIdForPath(const QString& path) const
{
    return QString(QCryptographicHash::hash(
        path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

void ComicReader::saveCurrentProgress()
{
    if (!m_bridge || m_pageNames.isEmpty()) return;
    QJsonObject data;
    data["page"] = m_currentPage;
    data["pageCount"] = m_pageNames.size();
    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), data);
}

int ComicReader::restoreSavedPage()
{
    if (!m_bridge) return 0;
    QJsonObject data = m_bridge->progress("comics", itemIdForPath(m_cbzPath));
    if (data.isEmpty()) return 0;
    int page = data["page"].toInt(0);
    if (page >= m_pageNames.size()) page = 0;
    return page;
}

// ── Fit Modes ───────────────────────────────────────────────────────────────

void ComicReader::cycleFitMode()
{
    switch (m_fitMode) {
    case FitMode::FitPage:   m_fitMode = FitMode::FitWidth;  break;
    case FitMode::FitWidth:  m_fitMode = FitMode::FitHeight; break;
    case FitMode::FitHeight: m_fitMode = FitMode::FitPage;   break;
    }
    static const QMap<FitMode, QString> names = {
        {FitMode::FitPage,   "Fit Page"},
        {FitMode::FitWidth,  "Fit Width"},
        {FitMode::FitHeight, "Fit Height"},
    };
    showToast(names[m_fitMode]);
    displayCurrentPage();
}

void ComicReader::showToast(const QString& text)
{
    if (!m_toastLabel) {
        m_toastLabel = new QLabel(this);
        m_toastLabel->setAlignment(Qt::AlignCenter);
        m_toastLabel->setStyleSheet(
            "background: rgba(0,0,0,0.75); color: white; "
            "border-radius: 12px; padding: 8px 20px; font-size: 13px;");
        connect(&m_toastTimer, &QTimer::timeout, m_toastLabel, &QLabel::hide);
    }
    m_toastLabel->setText(text);
    m_toastLabel->adjustSize();
    m_toastLabel->move((width() - m_toastLabel->width()) / 2,
                       height() / 2 - m_toastLabel->height() / 2);
    m_toastLabel->show();
    m_toastLabel->raise();
    m_toastTimer.start();
}

// ── Double Page Mode ────────────────────────────────────────────────────────

void ComicReader::toggleDoublePageMode()
{
    m_doublePageMode = !m_doublePageMode;
    showToast(m_doublePageMode ? "Double Page" : "Single Page");
    showPage(m_currentPage);
}

// ── Go-to-Page Dialog ───────────────────────────────────────────────────────

void ComicReader::showGoToDialog()
{
    if (!m_gotoOverlay) {
        m_gotoOverlay = new QWidget(this);
        m_gotoOverlay->setStyleSheet("background: rgba(0,0,0,0.70); border-radius: 12px;");
        m_gotoOverlay->setFixedSize(260, 80);

        auto* layout = new QVBoxLayout(m_gotoOverlay);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(8);

        auto* label = new QLabel(m_gotoOverlay);
        label->setObjectName("gotoLabel");
        label->setStyleSheet("color: rgba(255,255,255,0.78); font-size: 12px; background: transparent;");
        layout->addWidget(label);

        m_gotoInput = new QLineEdit(m_gotoOverlay);
        m_gotoInput->setStyleSheet(
            "background: rgba(255,255,255,0.10); color: white; "
            "border: 1px solid rgba(255,255,255,0.20); border-radius: 6px; "
            "padding: 4px 8px; font-size: 13px;");
        layout->addWidget(m_gotoInput);

        connect(m_gotoInput, &QLineEdit::returnPressed, this, [this]() {
            bool ok;
            int page = m_gotoInput->text().toInt(&ok);
            if (ok && page >= 1 && page <= m_pageNames.size())
                showPage(page - 1);
            hideGoToDialog();
        });
    }

    if (auto* label = m_gotoOverlay->findChild<QLabel*>("gotoLabel"))
        label->setText(QString("Go to page (1-%1):").arg(m_pageNames.size()));

    m_gotoInput->clear();
    m_gotoOverlay->move((width() - m_gotoOverlay->width()) / 2,
                        (height() - m_gotoOverlay->height()) / 2);
    m_gotoOverlay->show();
    m_gotoOverlay->raise();
    m_gotoInput->setFocus();
}

void ComicReader::hideGoToDialog()
{
    if (m_gotoOverlay) m_gotoOverlay->hide();
    setFocus();
}

// ── Volume Navigation ───────────────────────────────────────────────────────

void ComicReader::openVolumeByIndex(int volumeIndex)
{
    if (volumeIndex < 0 || volumeIndex >= m_seriesCbzList.size()) return;
    saveCurrentProgress();
    openBook(m_seriesCbzList[volumeIndex], m_seriesCbzList, m_seriesName);
}

void ComicReader::prevVolume()
{
    int idx = m_seriesCbzList.indexOf(m_cbzPath);
    if (idx > 0) openVolumeByIndex(idx - 1);
}

void ComicReader::nextVolume()
{
    int idx = m_seriesCbzList.indexOf(m_cbzPath);
    if (idx >= 0 && idx < m_seriesCbzList.size() - 1)
        openVolumeByIndex(idx + 1);
}

// ── Toolbar ─────────────────────────────────────────────────────────────────

void ComicReader::showToolbar()
{
    m_toolbar->show();
    m_toolbar->raise();
    if (m_toastLabel) m_toastLabel->raise();
    m_hideTimer.start();
}

void ComicReader::hideToolbar()
{
    m_toolbar->hide();
}

// ── Events ──────────────────────────────────────────────────────────────────

void ComicReader::keyPressEvent(QKeyEvent* event)
{
    showToolbar();

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_G) {
        showGoToDialog();
        return;
    }

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
        if (m_gotoOverlay && m_gotoOverlay->isVisible())
            hideGoToDialog();
        else {
            saveCurrentProgress();
            emit closeRequested();
        }
        break;
    case Qt::Key_F:
        cycleFitMode();
        break;
    case Qt::Key_M:
        toggleDoublePageMode();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ComicReader::wheelEvent(QWheelEvent* event)
{
    showToolbar();

    // In fit-width mode, let scroll area handle vertical scrolling
    if (m_fitMode == FitMode::FitWidth && m_scrollArea) {
        auto* vbar = m_scrollArea->verticalScrollBar();
        if (vbar && vbar->maximum() > 0) {
            int val = vbar->value();
            if (event->angleDelta().y() < 0 && val >= vbar->maximum())
                nextPage();
            else if (event->angleDelta().y() > 0 && val <= vbar->minimum())
                prevPage();
            else {
                // Let SmoothScrollArea handle the smooth scrolling
                m_scrollArea->handleWheel(event);
            }
            return;
        }
    }

    if (event->angleDelta().y() < 0)
        nextPage();
    else if (event->angleDelta().y() > 0)
        prevPage();
}

void ComicReader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_scrollArea->setGeometry(0, 0, width(), height());
    m_toolbar->setGeometry(0, height() - m_toolbar->height(),
                           width(), m_toolbar->height());
    displayCurrentPage();
}

void ComicReader::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showToolbar();
}
