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
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPainter>
#include <QScrollBar>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QGraphicsOpacityEffect>
#include <QColor>
#include <cmath>

static constexpr double SPREAD_RATIO = 1.08;
static constexpr double COUPLING_MIN_CONFIDENCE = 0.12;
static constexpr int COUPLING_PROBE_MAX_PAGES = 8;
static constexpr int COUPLING_MAX_SAMPLES = 4;
static constexpr int COUPLING_MAX_PROBE_ATTEMPTS = 6;
static const int PORTRAIT_PRESETS[] = {50, 60, 70, 74, 78, 90, 100};

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

    m_panDrainTimer.setInterval(16);
    m_panDrainTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_panDrainTimer, &QTimer::timeout, this, &ComicReader::drainPan);
}

void ComicReader::buildUI()
{
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

    // Toolbar
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

    // Portrait width button (single-page mode only)
    m_portraitBtn = makeBtn(QString::number(m_portraitWidthPct) + "%", 48);
    m_portraitBtn->setToolTip("Page width");
    connect(m_portraitBtn, &QPushButton::clicked, this, &ComicReader::showPortraitWidthMenu);
    tbLayout->addWidget(m_portraitBtn);

    m_nextVolBtn = makeBtn(QString(QChar(0x00BB)), 32);
    m_nextVolBtn->setToolTip("Next volume");
    m_nextVolBtn->hide();
    connect(m_nextVolBtn, &QPushButton::clicked, this, &ComicReader::nextVolume);
    tbLayout->addWidget(m_nextVolBtn);
}

// ── Open Book ───────────────────────────────────────────────────────────────

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
    m_spreadOverrides.clear();
    m_couplingMode = "auto";
    m_couplingPhase = "normal";
    m_couplingConfidence = 0.0f;
    m_couplingResolved = false;
    m_couplingProbeAttempts = 0;
    resetZoomPan();

    m_pageMeta.clear();
    m_pageMeta.resize(m_pageNames.size());
    for (int i = 0; i < m_pageNames.size(); ++i) {
        m_pageMeta[i].index = i;
        m_pageMeta[i].filename = m_pageNames[i];
    }
    invalidatePairing();

    if (m_pageNames.isEmpty()) {
        m_imageLabel->setText("No pages found in this archive");
        updatePageLabel();
        return;
    }

    int volIdx = m_seriesCbzList.indexOf(m_cbzPath);
    m_prevVolBtn->setVisible(!m_seriesCbzList.isEmpty() && volIdx > 0);
    m_nextVolBtn->setVisible(!m_seriesCbzList.isEmpty() && volIdx < m_seriesCbzList.size() - 1);

    // Update toolbar visibility for mode
    m_portraitBtn->setVisible(!m_doublePageMode);

    int startPage = restoreSavedPage();
    showPage(startPage);
    showToolbar();
}

// ── Canonical Pairing ───────────────────────────────────────────────────────

bool ComicReader::resolveSpread(int index) const
{
    if (index <= 0 || index >= m_pageMeta.size()) return false;
    auto it = m_spreadOverrides.find(index);
    if (it != m_spreadOverrides.end()) return it.value();
    return m_pageMeta[index].isSpread;
}

bool ComicReader::isSpreadIndex(int index) const { return resolveSpread(index); }

void ComicReader::invalidatePairing()
{
    m_canonicalUnits.clear();
    m_unitByPage.clear();
}

void ComicReader::buildCanonicalPairingUnits()
{
    m_canonicalUnits.clear();
    m_unitByPage.clear();

    int total = m_pageNames.size();
    if (total == 0) return;

    int nudge = (m_couplingPhase == "shifted") ? 1 : 0;
    int extraSlots = 0;
    int idx = 0;

    while (idx < total) {
        TwoPagePair unit;

        if (idx == 0) {
            // Cover always alone
            unit.rightIndex = 0;
            unit.coverAlone = true;
            m_unitByPage[0] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            idx++;
            continue;
        }

        bool isSpr = resolveSpread(idx);
        if (isSpr) {
            unit.rightIndex = idx;
            unit.isSpread = true;
            m_unitByPage[idx] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            extraSlots++;
            idx++;
            continue;
        }

        int parity = (idx + extraSlots + nudge) % 2;
        if (parity == 1 && idx + 1 < total && !resolveSpread(idx + 1)) {
            // Pair two pages
            unit.rightIndex = idx;
            unit.leftIndex = idx + 1;
            m_unitByPage[idx] = m_canonicalUnits.size();
            m_unitByPage[idx + 1] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            idx += 2;
        } else {
            // Single page
            unit.rightIndex = idx;
            unit.unpairedSingle = true;
            m_unitByPage[idx] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            idx++;
        }
    }
}

const TwoPagePair* ComicReader::pairForPage(int pageIndex) const
{
    auto it = m_unitByPage.find(pageIndex);
    if (it == m_unitByPage.end() || it.value() >= m_canonicalUnits.size())
        return nullptr;
    return &m_canonicalUnits[it.value()];
}

int ComicReader::navigateToUnit(int unitIndex)
{
    if (unitIndex < 0 || unitIndex >= m_canonicalUnits.size())
        return m_currentPage;
    return m_canonicalUnits[unitIndex].rightIndex;
}

// ── Show Page ───────────────────────────────────────────────────────────────

void ComicReader::requestDecode(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pageNames.size()) return;
    if (m_cache.contains(pageIndex)) return;

    auto* task = new DecodeTask(pageIndex, m_cbzPath, m_pageNames[pageIndex]);
    connect(&task->notifier, &DecodeTaskSignals::decoded,
            this, &ComicReader::onPageDecoded, Qt::QueuedConnection);
    m_decodePool.start(task);
}

void ComicReader::onPageDecoded(int pageIndex, const QPixmap& pixmap, int w, int h)
{
    m_cache.insert(pageIndex, pixmap);

    if (pageIndex >= 0 && pageIndex < m_pageMeta.size()) {
        m_pageMeta[pageIndex].width = w;
        m_pageMeta[pageIndex].height = h;
        m_pageMeta[pageIndex].isSpread = (h > 0 && static_cast<double>(w) / h > SPREAD_RATIO);
        m_pageMeta[pageIndex].decoded = true;
    }

    // Try auto-coupling after pages decode
    if (!m_couplingResolved && m_couplingMode == "auto")
        maybeRunAutoCoupling();

    if (pageIndex == m_currentPage) {
        m_currentPixmap = pixmap;
        displayCurrentPage();
    }
    if (m_doublePageMode && m_secondPixmap.isNull()) {
        auto* pair = pairForPage(m_currentPage);
        if (pair && pair->leftIndex == pageIndex) {
            m_secondPixmap = pixmap;
            displayCurrentPage();
        }
    }
}

int ComicReader::pageAdvanceCount() const
{
    if (!m_doublePageMode) return 1;
    auto* pair = pairForPage(m_currentPage);
    if (!pair) return 1;
    if (pair->leftIndex >= 0) return 2; // paired
    return 1; // single/spread/cover
}

QPixmap ComicReader::compositeDoublePages(const QPixmap& left, const QPixmap& right)
{
    QPixmap pageL = m_rtl ? right : left;
    QPixmap pageR = m_rtl ? left : right;

    int h = qMax(pageL.height(), pageR.height());
    int w = pageL.width() + pageR.width() + 4;
    QPixmap combined(w, h);
    combined.fill(Qt::black);
    QPainter painter(&combined);
    painter.drawPixmap(0, (h - pageL.height()) / 2, pageL);
    painter.drawPixmap(pageL.width() + 4, (h - pageR.height()) / 2, pageR);
    return combined;
}

void ComicReader::showPage(int index)
{
    if (m_pageNames.isEmpty()) return;
    index = qBound(0, index, m_pageNames.size() - 1);
    m_currentPage = index;
    m_secondPixmap = QPixmap();
    resetZoomPan();

    // Ensure pairing is built
    if (m_canonicalUnits.isEmpty() && m_doublePageMode)
        buildCanonicalPairingUnits();

    // Load current page
    m_cache.pin(index);
    if (m_cache.contains(index)) {
        m_currentPixmap = m_cache.get(index);
    } else {
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

    // Load second page for double-page pairs
    if (m_doublePageMode) {
        auto* pair = pairForPage(index);
        if (pair && pair->leftIndex >= 0) {
            int secondIdx = pair->leftIndex;
            m_cache.pin(secondIdx);
            if (m_cache.contains(secondIdx)) {
                m_secondPixmap = m_cache.get(secondIdx);
            } else {
                QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageNames[secondIdx]);
                QPixmap pix;
                pix.loadFromData(data);
                if (!pix.isNull()) {
                    m_cache.insert(secondIdx, pix);
                    if (secondIdx < m_pageMeta.size()) {
                        m_pageMeta[secondIdx].width = pix.width();
                        m_pageMeta[secondIdx].height = pix.height();
                        m_pageMeta[secondIdx].isSpread = (pix.height() > 0 &&
                            static_cast<double>(pix.width()) / pix.height() > SPREAD_RATIO);
                        m_pageMeta[secondIdx].decoded = true;
                    }
                    m_secondPixmap = pix;
                }
            }
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

    // Portrait width adjustment (single-page mode)
    if (!m_doublePageMode && m_fitMode == FitMode::FitPage) {
        double frac = m_portraitWidthPct / 100.0;
        int targetW = static_cast<int>(availW * frac);
        int targetH = availH;
        QPixmap scaled = composite.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_imageLabel->setPixmap(scaled);
        m_imageLabel->resize(scaled.size());
    } else {
        // Zoom handling for double-page mode
        double zoom = m_doublePageMode ? m_zoomPct / 100.0 : 1.0;

        QPixmap scaled;
        switch (m_fitMode) {
        case FitMode::FitPage:
            scaled = composite.scaled(static_cast<int>(availW * zoom),
                                       static_cast<int>(availH * zoom),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
            break;
        case FitMode::FitWidth:
            scaled = composite.scaledToWidth(static_cast<int>(availW * zoom), Qt::SmoothTransformation);
            break;
        case FitMode::FitHeight:
            scaled = composite.scaledToHeight(static_cast<int>(availH * zoom), Qt::SmoothTransformation);
            break;
        }

        m_imageLabel->setPixmap(scaled);
        m_imageLabel->resize(scaled.size());
    }

    if (auto* vbar = m_scrollArea->verticalScrollBar())
        vbar->setValue(0);
}

void ComicReader::prefetchNeighbors()
{
    int advance = pageAdvanceCount();
    for (int i = 1; i <= 4; ++i) {
        int idx = m_currentPage + advance + i - 1;
        if (idx < m_pageNames.size()) requestDecode(idx);
    }
    if (m_currentPage > 0) requestDecode(m_currentPage - 1);
}

void ComicReader::nextPage()
{
    if (!m_doublePageMode) {
        if (m_currentPage + 1 < m_pageNames.size())
            showPage(m_currentPage + 1);
        return;
    }
    auto it = m_unitByPage.find(m_currentPage);
    if (it != m_unitByPage.end() && it.value() + 1 < m_canonicalUnits.size())
        showPage(navigateToUnit(it.value() + 1));
}

void ComicReader::prevPage()
{
    if (!m_doublePageMode) {
        if (m_currentPage > 0) showPage(m_currentPage - 1);
        return;
    }
    auto it = m_unitByPage.find(m_currentPage);
    if (it != m_unitByPage.end() && it.value() > 0)
        showPage(navigateToUnit(it.value() - 1));
}

void ComicReader::updatePageLabel()
{
    if (m_pageNames.isEmpty()) { m_pageLabel->setText("No pages"); return; }
    if (m_doublePageMode && !m_secondPixmap.isNull()) {
        auto* pair = pairForPage(m_currentPage);
        if (pair && pair->leftIndex >= 0) {
            m_pageLabel->setText(QString("Pages %1-%2 / %3")
                .arg(pair->rightIndex + 1).arg(pair->leftIndex + 1).arg(m_pageNames.size()));
            return;
        }
    }
    m_pageLabel->setText(QString("Page %1 / %2").arg(m_currentPage + 1).arg(m_pageNames.size()));
}

// ── Progress ────────────────────────────────────────────────────────────────

QString ComicReader::itemIdForPath(const QString& path) const
{
    return QString(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
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
        {FitMode::FitPage, "Fit Page"}, {FitMode::FitWidth, "Fit Width"}, {FitMode::FitHeight, "Fit Height"},
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
            "background: rgba(0,0,0,0.75); color: white; border-radius: 12px; padding: 8px 20px; font-size: 13px;");
        connect(&m_toastTimer, &QTimer::timeout, m_toastLabel, &QLabel::hide);
    }
    m_toastLabel->setText(text);
    m_toastLabel->adjustSize();
    m_toastLabel->move((width() - m_toastLabel->width()) / 2, height() / 2 - m_toastLabel->height() / 2);
    m_toastLabel->show();
    m_toastLabel->raise();
    m_toastTimer.start();
}

// ── Double Page Mode ────────────────────────────────────────────────────────

void ComicReader::toggleDoublePageMode()
{
    m_doublePageMode = !m_doublePageMode;
    m_portraitBtn->setVisible(!m_doublePageMode);
    if (m_doublePageMode)
        buildCanonicalPairingUnits();
    else
        invalidatePairing();
    showToast(m_doublePageMode ? "Double Page" : "Single Page");
    showPage(m_currentPage);
}

// ── Coupling Nudge ──────────────────────────────────────────────────────────

void ComicReader::toggleCouplingNudge()
{
    if (!m_doublePageMode) return;
    m_couplingPhase = (m_couplingPhase == "normal") ? "shifted" : "normal";
    m_couplingMode = "manual";
    m_couplingConfidence = 1.0f;
    invalidatePairing();
    buildCanonicalPairingUnits();
    showToast(m_couplingPhase == "normal" ? "Normal Pairing" : "Shifted Pairing");
    showPage(m_currentPage);
}

// ── Auto-Coupling ───────────────────────────────────────────────────────────

double ComicReader::edgeContinuityCost(const QPixmap& leftPix, const QPixmap& rightPix)
{
    if (leftPix.isNull() || rightPix.isNull()) return 1.0;
    QImage left = leftPix.toImage().scaled(8, 96, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QImage right = rightPix.toImage().scaled(8, 96, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    double cost = 0.0;
    for (int y = 0; y < 96; ++y) {
        QColor lc = left.pixelColor(7, y);
        QColor rc = right.pixelColor(0, y);
        double lumL = 0.299 * lc.redF() + 0.587 * lc.greenF() + 0.114 * lc.blueF();
        double lumR = 0.299 * rc.redF() + 0.587 * rc.greenF() + 0.114 * rc.blueF();
        cost += std::abs(lumL - lumR);
    }
    return cost / 96.0;
}

QVector<int> ComicReader::autoPhaseSampleIndexes(bool shifted)
{
    QVector<int> samples;
    int total = m_pageNames.size();
    int probeLimit = qMin(total - 1, COUPLING_PROBE_MAX_PAGES);
    int nudge = shifted ? 1 : 0;
    int extraSlots = 0;

    for (int idx = 1; idx < probeLimit && samples.size() < COUPLING_MAX_SAMPLES; ++idx) {
        if (resolveSpread(idx)) { extraSlots++; continue; }
        int parity = (idx + extraSlots + nudge) % 2;
        if (parity == 1 && idx + 1 < total && !resolveSpread(idx + 1))
            samples.append(idx);
    }
    return samples;
}

double ComicReader::scorePhase(bool shifted)
{
    auto samples = autoPhaseSampleIndexes(shifted);
    if (samples.isEmpty()) return 1.0;

    double totalCost = 0.0;
    int validCount = 0;
    for (int rightIdx : samples) {
        int leftIdx = rightIdx + 1;
        if (!m_cache.contains(rightIdx) || !m_cache.contains(leftIdx)) continue;
        QPixmap rPix = m_cache.get(rightIdx);
        QPixmap lPix = m_cache.get(leftIdx);
        totalCost += edgeContinuityCost(rPix, lPix);
        validCount++;
    }
    return validCount > 0 ? totalCost / validCount : 1.0;
}

void ComicReader::maybeRunAutoCoupling()
{
    if (m_couplingResolved || m_couplingMode != "auto") return;
    if (m_pageNames.size() <= 3) { m_couplingResolved = true; return; }

    double normalScore = scorePhase(false);
    double shiftedScore = scorePhase(true);

    // Check if we have enough samples
    auto normalSamples = autoPhaseSampleIndexes(false);
    auto shiftedSamples = autoPhaseSampleIndexes(true);
    bool hasNormalData = false, hasShiftedData = false;
    for (int idx : normalSamples) if (m_cache.contains(idx) && m_cache.contains(idx+1)) { hasNormalData = true; break; }
    for (int idx : shiftedSamples) if (m_cache.contains(idx) && m_cache.contains(idx+1)) { hasShiftedData = true; break; }

    if (!hasNormalData || !hasShiftedData) {
        m_couplingProbeAttempts++;
        if (m_couplingProbeAttempts >= COUPLING_MAX_PROBE_ATTEMPTS) {
            m_couplingResolved = true;
            return;
        }
        // Request probe pages
        for (bool sh : {false, true}) {
            for (int idx : autoPhaseSampleIndexes(sh)) {
                requestDecode(idx);
                requestDecode(idx + 1);
            }
        }
        return;
    }

    double base = std::max(0.001, normalScore + shiftedScore);
    double confidence = std::abs(normalScore - shiftedScore) / base;

    if (shiftedScore < normalScore && confidence >= COUPLING_MIN_CONFIDENCE) {
        m_couplingPhase = "shifted";
    } else {
        m_couplingPhase = "normal";
    }
    m_couplingConfidence = static_cast<float>(confidence);
    m_couplingResolved = true;

    if (m_doublePageMode) {
        invalidatePairing();
        buildCanonicalPairingUnits();
        showPage(m_currentPage);
    }
}

// ── Spread Override ─────────────────────────────────────────────────────────

void ComicReader::showSpreadOverrideMenu(int pageIndex, const QPoint& globalPos)
{
    if (pageIndex <= 0 || pageIndex >= m_pageMeta.size()) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: rgba(8,8,8,0.92); border: 1px solid rgba(255,255,255,0.12); border-radius: 8px; padding: 4px; }"
        "QMenu::item { color: #eee; padding: 6px 16px; font-size: 11px; }"
        "QMenu::item:selected { background: rgba(199,167,107,0.22); }"
    );

    bool currentlySpread = resolveSpread(pageIndex);
    auto* toggleAction = menu.addAction(currentlySpread ? "Mark as Normal" : "Mark as Spread");
    menu.addSeparator();
    auto* resetAction = menu.addAction("Reset All Overrides");

    auto* chosen = menu.exec(globalPos);
    if (chosen == toggleAction) {
        m_spreadOverrides[pageIndex] = !currentlySpread;
        invalidatePairing();
        if (m_doublePageMode) {
            buildCanonicalPairingUnits();
            showPage(m_currentPage);
        }
    } else if (chosen == resetAction) {
        m_spreadOverrides.clear();
        invalidatePairing();
        if (m_doublePageMode) {
            buildCanonicalPairingUnits();
            showPage(m_currentPage);
        }
    }
}

// ── Reading Direction ───────────────────────────────────────────────────────

void ComicReader::toggleReadingDirection()
{
    if (!m_doublePageMode) return;
    m_rtl = !m_rtl;
    showToast(m_rtl ? "Right to Left" : "Left to Right");
    displayCurrentPage();
}

// ── Portrait Width ──────────────────────────────────────────────────────────

void ComicReader::showPortraitWidthMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: rgba(8,8,8,0.92); border: 1px solid rgba(255,255,255,0.12); border-radius: 8px; padding: 4px; }"
        "QMenu::item { color: #eee; padding: 6px 16px; font-size: 11px; }"
        "QMenu::item:selected { background: rgba(199,167,107,0.22); }"
        "QMenu::item:checked { color: #c7a76b; }"
    );
    for (int pct : PORTRAIT_PRESETS) {
        auto* action = menu.addAction(QString::number(pct) + "%");
        action->setCheckable(true);
        action->setChecked(pct == m_portraitWidthPct);
        action->setData(pct);
    }
    QPoint pos = m_portraitBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height()));
    auto* chosen = menu.exec(pos);
    if (chosen) setPortraitWidthPct(chosen->data().toInt());
}

void ComicReader::setPortraitWidthPct(int pct)
{
    pct = qBound(50, pct, 100);
    if (pct == m_portraitWidthPct) return;
    m_portraitWidthPct = pct;
    m_portraitBtn->setText(QString::number(pct) + "%");
    displayCurrentPage();
}

// ── Zoom & Pan ──────────────────────────────────────────────────────────────

void ComicReader::setZoom(int pct)
{
    m_zoomPct = qBound(100, pct, 260);
    displayCurrentPage();
}

void ComicReader::zoomBy(int delta)
{
    if (!m_doublePageMode) return;
    setZoom(m_zoomPct + delta);
    showToast(QString("Zoom: %1%").arg(m_zoomPct));
}

void ComicReader::resetZoomPan()
{
    m_zoomPct = 100;
    m_panX = m_panY = m_panXMax = m_panYMax = m_pendingPanPx = 0.0;
    m_panDrainTimer.stop();
}

void ComicReader::setPan(double panY)
{
    m_panY = qBound(0.0, panY, m_panYMax);
    if (auto* vbar = m_scrollArea->verticalScrollBar())
        vbar->setValue(static_cast<int>(m_panY));
}

void ComicReader::drainPan()
{
    double maxStep = std::max(66.0, height() * 0.22);
    double take = m_pendingPanPx * 0.38;
    take = qBound(-maxStep, take, maxStep);
    if (std::abs(take) < 2.0) take = m_pendingPanPx;
    m_pendingPanPx -= take;
    setPan(m_panY + take);
    if (std::abs(m_pendingPanPx) < 0.5) {
        m_pendingPanPx = 0.0;
        m_panDrainTimer.stop();
    }
}

// ── Click Zones ─────────────────────────────────────────────────────────────

QString ComicReader::clickZone(const QPoint& pos) const
{
    int w = width();
    double third = w / 3.0;
    if (pos.x() < third) return "left";
    if (pos.x() >= w - third) return "right";
    return "mid";
}

void ComicReader::flashClickZone(const QString& side)
{
    auto* flash = new QWidget(this);
    flash->setStyleSheet("background: rgba(255,255,255,0.08);");

    int w = width() / 3;
    if (side == "left") flash->setGeometry(0, 0, w, height());
    else if (side == "right") flash->setGeometry(width() - w, 0, w, height());
    else return;

    flash->show();
    flash->raise();

    auto* effect = new QGraphicsOpacityEffect(flash);
    flash->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", flash);
    anim->setDuration(250);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, flash, &QWidget::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
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
            "border: 1px solid rgba(255,255,255,0.20); border-radius: 6px; padding: 4px 8px; font-size: 13px;");
        layout->addWidget(m_gotoInput);
        connect(m_gotoInput, &QLineEdit::returnPressed, this, [this]() {
            bool ok; int page = m_gotoInput->text().toInt(&ok);
            if (ok && page >= 1 && page <= m_pageNames.size()) showPage(page - 1);
            hideGoToDialog();
        });
    }
    if (auto* label = m_gotoOverlay->findChild<QLabel*>("gotoLabel"))
        label->setText(QString("Go to page (1-%1):").arg(m_pageNames.size()));
    m_gotoInput->clear();
    m_gotoOverlay->move((width() - m_gotoOverlay->width()) / 2, (height() - m_gotoOverlay->height()) / 2);
    m_gotoOverlay->show(); m_gotoOverlay->raise(); m_gotoInput->setFocus();
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
    if (idx >= 0 && idx < m_seriesCbzList.size() - 1) openVolumeByIndex(idx + 1);
}

// ── Toolbar ─────────────────────────────────────────────────────────────────

void ComicReader::showToolbar()
{
    m_toolbar->show(); m_toolbar->raise();
    if (m_toastLabel) m_toastLabel->raise();
    m_hideTimer.start();
}

void ComicReader::hideToolbar() { m_toolbar->hide(); }

// ── Events ──────────────────────────────────────────────────────────────────

void ComicReader::keyPressEvent(QKeyEvent* event)
{
    showToolbar();

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_G) {
        showGoToDialog(); return;
    }

    switch (event->key()) {
    case Qt::Key_Right: case Qt::Key_Space: case Qt::Key_PageDown: nextPage(); break;
    case Qt::Key_Left: case Qt::Key_PageUp: prevPage(); break;
    case Qt::Key_Home: showPage(0); break;
    case Qt::Key_End: showPage(m_pageNames.size() - 1); break;
    case Qt::Key_Escape:
        if (m_gotoOverlay && m_gotoOverlay->isVisible()) hideGoToDialog();
        else { saveCurrentProgress(); emit closeRequested(); }
        break;
    case Qt::Key_F: cycleFitMode(); break;
    case Qt::Key_M: toggleDoublePageMode(); break;
    case Qt::Key_P: toggleCouplingNudge(); break;
    case Qt::Key_I: toggleReadingDirection(); break;
    case Qt::Key_Up:
        if (m_doublePageMode && m_zoomPct > 100) { m_pendingPanPx -= 80; if (!m_panDrainTimer.isActive()) m_panDrainTimer.start(); }
        break;
    case Qt::Key_Down:
        if (m_doublePageMode && m_zoomPct > 100) { m_pendingPanPx += 80; if (!m_panDrainTimer.isActive()) m_panDrainTimer.start(); }
        break;
    default: QWidget::keyPressEvent(event);
    }
}

void ComicReader::wheelEvent(QWheelEvent* event)
{
    showToolbar();

    // Ctrl+wheel: zoom in double-page mode
    if (m_doublePageMode && event->modifiers() & Qt::ControlModifier) {
        zoomBy(event->angleDelta().y() > 0 ? 20 : -20);
        event->accept();
        return;
    }

    // Fit-width scrolling
    if (m_fitMode == FitMode::FitWidth && m_scrollArea) {
        auto* vbar = m_scrollArea->verticalScrollBar();
        if (vbar && vbar->maximum() > 0) {
            int val = vbar->value();
            if (event->angleDelta().y() < 0 && val >= vbar->maximum()) nextPage();
            else if (event->angleDelta().y() > 0 && val <= vbar->minimum()) prevPage();
            else m_scrollArea->handleWheel(event);
            return;
        }
    }

    if (event->angleDelta().y() < 0) nextPage();
    else if (event->angleDelta().y() > 0) prevPage();
}

void ComicReader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_doublePageMode) {
        QString zone = clickZone(event->pos());
        if (zone == "left") {
            flashClickZone("left");
            m_rtl ? nextPage() : prevPage();
            return;
        } else if (zone == "right") {
            flashClickZone("right");
            m_rtl ? prevPage() : nextPage();
            return;
        } else {
            // Mid click: toggle toolbar
            if (m_toolbar->isVisible()) hideToolbar(); else showToolbar();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ComicReader::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_doublePageMode) {
        showSpreadOverrideMenu(m_currentPage, event->globalPos());
        return;
    }
    QWidget::contextMenuEvent(event);
}

void ComicReader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_scrollArea->setGeometry(0, 0, width(), height());
    m_toolbar->setGeometry(0, height() - m_toolbar->height(), width(), m_toolbar->height());
    displayCurrentPage();
}

void ComicReader::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showToolbar();
}
