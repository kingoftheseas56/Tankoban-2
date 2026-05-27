// src/ui/pages/comics/VolumeTile.cpp

#include "ui/pages/comics/VolumeTile.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QStyle>
#include <QStandardPaths>
#include <QStringList>

#include <optional>

#include "core/manga/MangaDownloadIndex.h"
#include "ui/readers/comic_progress_key.h"

namespace tankoban::ui::comics {

namespace {

constexpr int kCoverWidth = 76;
constexpr int kCoverHeight = 108;
constexpr int kRowHeight = 124;
constexpr int kStateIconSize = 16;

QPixmap downloadedIcon()
{
    QPixmap pix(kStateIconSize, kStateIconSize);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(QStringLiteral("#66ffaa")), 1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    QPainterPath path;
    path.moveTo(3.0, 8.2);
    path.lineTo(6.3, 11.6);
    path.lineTo(13.5, 4.4);
    painter.drawPath(path);
    return pix;
}

QPixmap downloadingIcon(int progressPct)
{
    const int pct = qBound(0, progressPct, 100);
    const int visualPct = pct > 0 ? pct : 18;

    QPixmap pix(kStateIconSize, kStateIconSize);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF ringRect(2.5, 2.5, 11.0, 11.0);

    QPen trackPen(QColor(QStringLiteral("#2a3a4a")), 1.8);
    trackPen.setCapStyle(Qt::RoundCap);
    painter.setPen(trackPen);
    painter.drawEllipse(ringRect);

    QPen progressPen(QColor(QStringLiteral("#5d8fc7")), 1.8);
    progressPen.setCapStyle(Qt::RoundCap);
    painter.setPen(progressPen);
    painter.drawArc(ringRect, 90 * 16, -qRound(360.0 * visualPct / 100.0 * 16.0));
    return pix;
}

int progressPercentFromText(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("(\\d{1,3})\\s*%"));
    const auto match = re.match(text);
    if (!match.hasMatch()) return 0;
    return qBound(0, match.captured(1).toInt(), 100);
}

QString resolveComicsDataDir()
{
    const QString envDir = qEnvironmentVariable("TANKOBAN_DATA_DIR");
    if (!envDir.isEmpty())
        return QDir(envDir).absolutePath();

    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .absoluteFilePath(QStringLiteral("Tankoban/data"));
}

double readProgressForPath(const QString& cbzPath)
{
    if (cbzPath.isEmpty()) return 0.0;

    QFile file(QDir(resolveComicsDataDir()).absoluteFilePath(QStringLiteral("progress.json")));
    if (!file.open(QIODevice::ReadOnly)) return 0.0;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return 0.0;

    const QJsonObject entry =
        doc.object().value(comicProgressKeyForPath(cbzPath)).toObject();
    if (entry.isEmpty()) return 0.0;

    const int page = entry.value(QStringLiteral("page")).toInt(0);
    const int pageCount = entry.value(QStringLiteral("pageCount")).toInt(0);
    if (entry.value(QStringLiteral("finished")).toBool(false) && pageCount > 0)
        return 1.0;
    if (page <= 0 || pageCount <= 0) return 0.0;
    return qBound(0.0, static_cast<double>(page) / static_cast<double>(pageCount), 1.0);
}

QString displayTitleForVolume(const VolumeTileData& data)
{
    if (data.title.compare(QStringLiteral("Volume X"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Volume X");
    if (!data.title.trimmed().isEmpty())
        return QStringLiteral("Volume %1 - %2").arg(data.volumeNumber).arg(data.title.trimmed());
    return QStringLiteral("Volume %1").arg(data.volumeNumber);
}

QString subtitleForVolume(const VolumeTileData& data)
{
    // COMICS_VOLUME_SYNOPSIS_POLISH 2026-05-26 (Agent 9).
    // Volume rows show ONLY the per-volume synopsis as the secondary line.
    // Chapter range and release date are intentionally excluded — they
    // cluttered the page. Caller hides the label when this returns empty.
    const QString synopsis = data.synopsis.simplified();
    if (synopsis.isEmpty())
        return {};

    constexpr int kMaxSynopsisChars = 160;
    if (synopsis.size() <= kMaxSynopsisChars)
        return synopsis;
    return synopsis.left(kMaxSynopsisChars - 1).trimmed() + QStringLiteral("…");
}

} // namespace

VolumeTileState::State VolumeTile::computeState(bool hasIndexEntry,
                                                const QString& statusText)
{
    if (hasIndexEntry) return VolumeTileState::Complete;

    if (statusText.startsWith(QStringLiteral("Queued"), Qt::CaseInsensitive))
        return VolumeTileState::Queued;
    if (statusText.startsWith(QStringLiteral("Downloading"), Qt::CaseInsensitive))
        return VolumeTileState::Downloading;
    if (statusText.startsWith(QStringLiteral("Failed"), Qt::CaseInsensitive))
        return VolumeTileState::Failed;

    return VolumeTileState::NotStarted;
}

VolumeTile::VolumeTile(const VolumeTileData& data, QWidget* parent)
    : QFrame(parent), m_data(data)
{
    setObjectName(QStringLiteral("VolumeTile"));
    buildUi();
}

bool VolumeTile::isChecked() const
{
    return m_checkbox && m_checkbox->isChecked();
}

void VolumeTile::setChecked(bool checked)
{
    if (m_checkbox) m_checkbox->setChecked(checked);
}

void VolumeTile::setCheckedQuiet(bool checked)
{
    if (!m_checkbox) return;
    const bool wasBlocked = m_checkbox->blockSignals(true);
    m_checkbox->setChecked(checked);
    m_checkbox->blockSignals(wasBlocked);
}

void VolumeTile::setVolumeState(const VolumeTileState& s)
{
    m_state = s;
    applyState();
}

void VolumeTile::setCoverFromDisk(const QString& coverPath)
{
    if (coverPath.isEmpty()) return;
    QPixmap pm(coverPath);
    if (pm.isNull()) return;
    m_coverPixmap = pm;
    refreshCoverPixmap();
}

void VolumeTile::setCoverFromUrl(const QString& url)
{
    if (!url.isEmpty()) m_data.coverUrl = url;
}

void VolumeTile::setCoverFromPixmap(const QPixmap& pm)
{
    if (pm.isNull()) return;
    m_coverPixmap = pm;
    refreshCoverPixmap();
}

void VolumeTile::setStatusText(const QString& text)
{
    m_state.statusText = text;
    const int pct = progressPercentFromText(text);
    if (pct > 0) m_state.progressPct = pct;
    if (m_state.cbzPath.isEmpty())
        m_state.state = computeState(/*hasIndexEntry=*/false, text);
    applyState();
}

void VolumeTile::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void VolumeTile::setMangaDownloadIndex(MangaDownloadIndex* idx)
{
    if (m_idx == idx) return;
    if (m_idx) disconnect(m_idx.data(), nullptr, this, nullptr);

    m_idx = idx;
    if (m_idx) {
        connect(m_idx.data(), &MangaDownloadIndex::entriesChanged,
                this, &VolumeTile::onIndexEntriesChanged,
                Qt::QueuedConnection);
        onIndexEntriesChanged();
    }
}

void VolumeTile::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
}

void VolumeTile::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit rowClicked(m_data.volumeNumber);
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void VolumeTile::onIndexEntriesChanged()
{
    if (!m_idx) {
        if (m_state.cbzPath.isEmpty())
            m_state.state = computeState(false, m_state.statusText);
        applyState();
        return;
    }

    auto entry = m_idx->entryForSeriesAndVolume(m_data.sourceId, m_data.seriesId,
                                                m_data.volumeNumber);

    // COMICS_WC_SOURCE_LABEL_FIX 2026-05-26 (Agent 9).
    // Pre-fix WeebCentral-packed downloads were registered with sourceId
    // "mangafire_catalog" instead of "weebcentral". Try the old identity
    // as a fallback so existing downloads still show as Complete without
    // requiring a JSON migration or re-download.
    if (!entry && m_data.sourceId == QLatin1String("weebcentral")) {
        entry = m_idx->entryForSeriesAndVolume(
            QStringLiteral("mangafire_catalog"), m_data.seriesId,
            m_data.volumeNumber);
    }

    if (entry) {
        m_state.state = VolumeTileState::Complete;
        m_state.cbzPath = entry->canonicalPath;
    } else if (m_state.cbzPath.isEmpty()) {
        m_state.state = computeState(false, m_state.statusText);
    }
    applyState();
}

void VolumeTile::onActionClicked()
{
    switch (m_state.state) {
    case VolumeTileState::NotStarted:   emit downloadRequested(m_data.volumeNumber); break;
    case VolumeTileState::Queued:
    case VolumeTileState::Downloading:  emit cancelRequested(m_data.volumeNumber);   break;
    case VolumeTileState::Complete:     emit openRequested(m_data.volumeNumber);     break;
    case VolumeTileState::Failed:       emit retryRequested(m_data.volumeNumber);    break;
    }
}

void VolumeTile::buildUi()
{
    setFrameShape(QFrame::NoFrame);
    setFixedHeight(kRowHeight);
    setCursor(Qt::PointingHandCursor);
    setProperty("selected", false);
    setStyleSheet(QStringLiteral(
        "QFrame#VolumeTile {"
        "  background: transparent;"
        "  border-bottom: 1px solid rgba(255,255,255,0.04);"
        "}"
        "QFrame#VolumeTile:hover { background: rgba(255,255,255,0.04); }"
        "QFrame#VolumeTile[selected=\"true\"] { background: rgba(138,106,255,0.10); }"
        "QCheckBox { background: transparent; spacing: 0; }"
        "QCheckBox::indicator { width: 14px; height: 14px;"
        "  border: 1px solid rgba(255,255,255,0.34); border-radius: 2px;"
        "  background: transparent; }"
        "QCheckBox::indicator:checked { background: #8a6aff; border-color: #8a6aff; }"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(12);

    m_checkbox = new QCheckBox(this);
    m_checkbox->setCursor(Qt::PointingHandCursor);
    m_checkbox->setFixedSize(20, 20);
    layout->addWidget(m_checkbox);

    const QString numberText =
        m_data.title.compare(QStringLiteral("Volume X"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("X")
            : QString::number(m_data.volumeNumber);
    m_numberLabel = new QLabel(numberText, this);
    m_numberLabel->setFixedWidth(32);
    m_numberLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_numberLabel->setStyleSheet(
        "color: rgba(255,255,255,0.42); font-size: 12px; font-weight: 600;");
    m_numberLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_numberLabel);

    m_coverLabel = new QLabel(this);
    m_coverLabel->setFixedSize(kCoverWidth, kCoverHeight);
    m_coverLabel->setStyleSheet(QStringLiteral("background: transparent; border-radius: 3px;"));
    m_coverLabel->setAlignment(Qt::AlignCenter);
    m_coverLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_coverLabel);

    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(5);

    m_titleLabel = new QLabel(displayTitleForVolume(m_data), this);
    m_titleLabel->setStyleSheet("color: #f0f0f0; font-size: 13px; font-weight: 700;");
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_titleLabel->setWordWrap(false);
    textLayout->addWidget(m_titleLabel);

    m_synopsisLabel = new QLabel(subtitleForVolume(m_data), this);
    m_synopsisLabel->setObjectName(QStringLiteral("VolumeTileSynopsis"));
    m_synopsisLabel->setStyleSheet(
        "QLabel#VolumeTileSynopsis { color: rgba(255,255,255,0.54); font-size: 11px; }");
    m_synopsisLabel->setWordWrap(true);
    m_synopsisLabel->setMaximumHeight(40);
    m_synopsisLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_synopsisLabel->setVisible(!m_synopsisLabel->text().isEmpty());
    textLayout->addWidget(m_synopsisLabel);
    textLayout->addStretch(1);

    layout->addLayout(textLayout, 1);

    m_stateIconLabel = new QLabel(this);
    m_stateIconLabel->setFixedSize(28, 28);
    m_stateIconLabel->setAlignment(Qt::AlignCenter);
    m_stateIconLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    m_stateIconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_stateIconLabel);

    connect(m_checkbox, &QCheckBox::toggled, this, [this](bool checked) {
        const bool shiftHeld =
            QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        emit toggled(checked);
        emit toggledShift(checked, shiftHeld);
    });

    refreshCoverPixmap();
    applyState();
}

void VolumeTile::applyState()
{
    refreshReadProgress();
    refreshCoverPixmap();
    refreshStateIcon();
}

void VolumeTile::refreshCoverPixmap()
{
    if (!m_coverLabel) return;

    QPixmap source = m_coverPixmap;
    if (source.isNull()) {
        source = QPixmap(kCoverWidth, kCoverHeight);
        source.fill(QColor(QStringLiteral("#1c1c22")));
        QPainter placeholder(&source);
        placeholder.setRenderHint(QPainter::Antialiasing);
        placeholder.setPen(QPen(QColor(QStringLiteral("#2f2f36")), 1));
        placeholder.drawRoundedRect(source.rect().adjusted(0, 0, -1, -1), 4, 4);
    }

    QPixmap canvas(kCoverWidth, kCoverHeight);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(canvas.rect(), 3, 3);
    painter.setClipPath(clip);
    painter.setOpacity(m_state.state == VolumeTileState::NotStarted ? 0.45 : 1.0);
    painter.drawPixmap(canvas.rect(),
                       source.scaled(canvas.size(),
                                     Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation));
    painter.setOpacity(1.0);
    if (m_readProgressFraction > 0.0) {
        const int progressW = qRound(canvas.width() * m_readProgressFraction);
        painter.fillRect(QRect(0, canvas.height() - 3, progressW, 3),
                         QColor(QStringLiteral("#8a6aff")));
    }
    painter.end();

    m_coverLabel->setPixmap(canvas);
}

void VolumeTile::refreshStateIcon()
{
    if (!m_stateIconLabel) return;

    if (m_state.state == VolumeTileState::Complete) {
        m_stateIconLabel->setPixmap(downloadedIcon());
    } else if (m_state.state == VolumeTileState::Downloading ||
               m_state.state == VolumeTileState::Queued) {
        m_stateIconLabel->setPixmap(downloadingIcon(m_state.progressPct));
    } else {
        m_stateIconLabel->clear();
    }
}

void VolumeTile::refreshReadProgress()
{
    m_readProgressFraction = readProgressForPath(m_state.cbzPath);
}

} // namespace tankoban::ui::comics
