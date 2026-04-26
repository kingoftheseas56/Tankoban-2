#include "ui/player/LoadingOverlay.h"

#include "core/DebugLogBuffer.h"

#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>
#include <QFontMetrics>

// STREAM_STALL_RECOVERY_UX instrumentation (Direction C, 2026-04-22). Traces
// stall-overlay lifecycle. Routed through DebugLogBuffer (REPO_HYGIENE P1.2,
// 2026-04-26) — the prior pattern wrote to a hardcoded developer path.
namespace {
void logStallDbg(const QString& line)
{
    DebugLogBuffer::instance().info("loading-overlay", line);
}
}  // namespace

LoadingOverlay::LoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    // Wake 2026-04-24 — simple-loading-bar geometry per Hemanth directive.
    // 260×6 is a thin centered indeterminate bar; minimal canvas intrusion
    // vs the prior 400×48 text-pill. Width tuned so the sweeper band
    // (30% = ~78 px) reads as a cohesive motion element rather than a
    // dot.
    setFixedSize(260, 6);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);

    // Indeterminate sweep: 0.0 → 1.0 → 0.0 every 1500 ms (standard
    // Material/iOS cadence), looped infinitely. Only runs while the
    // overlay is visible — fadeIn starts it, fadeOut's finished callback
    // stops it so the timer doesn't tick forever in the background.
    m_phaseAnim = new QPropertyAnimation(this, "phase", this);
    m_phaseAnim->setDuration(1500);
    m_phaseAnim->setStartValue(0.0);
    m_phaseAnim->setEndValue(1.0);
    m_phaseAnim->setLoopCount(-1);  // infinite
}

// STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — primary API. Sets the current
// stage + optionally updates filename. Mutates in place (no re-fade) when
// already visible to avoid flashing on every sub-stage transition during
// a fast open. Fades in from hidden.
void LoadingOverlay::setStage(Stage stage, const QString& filename)
{
    const bool transitioningToBuffering = (stage == Stage::Buffering && m_stage != Stage::Buffering);
    const bool enteringVisibleBuffering = (stage == Stage::Buffering && !m_visible);
    if (transitioningToBuffering || enteringVisibleBuffering) {
        logStallDbg(QString("setStage -> Buffering was_visible=%1 was_stage=%2 opacity=%3")
                        .arg(m_visible).arg(static_cast<int>(m_stage)).arg(m_opacity));
        m_stallPaintLogged = false;
    }
    m_stage = stage;
    if (!filename.isEmpty()) {
        // Display just the basename — full paths are noise in the
        // centered-over-canvas position. Streams (http://...) get their
        // tail after the last slash which tends to be a query-string'd
        // token; acceptable for rare-path visibility.
        m_filename = QFileInfo(filename).fileName();
    }
    if (m_visible && m_opacity >= 0.98) {
        // Already on-screen — swap text instantly, no re-fade. Typical
        // during Opening → Probing → OpeningDecoder → DecodingFirstFrame
        // transitions on a fast open where all 4 stages fire within a
        // few hundred ms. Re-fading on each would flicker.
        update();
    } else {
        m_visible = true;
        fadeIn();
    }
}

void LoadingOverlay::showLoading(const QString& filename)
{
    setStage(Stage::Opening, filename);
}

void LoadingOverlay::showBuffering()
{
    setStage(Stage::Buffering);
}

void LoadingOverlay::dismiss()
{
    if (!m_visible) return;
    logStallDbg(QString("dismiss was_visible=%1 stage=%2 opacity=%3 last_stall_piece=%4 last_peer_have=%5")
                    .arg(m_visible).arg(static_cast<int>(m_stage)).arg(m_opacity)
                    .arg(m_stallPiece).arg(m_stallPeerHaveCount));
    m_visible = false;
    m_stallPaintLogged = false;
    clearCacheProgress();     // stale % + ETA must not bleed into next buffering
    clearStallDiagnostic();   // STREAM_STALL_UX_FIX Batch 2 — same hygiene for stall text
    fadeOut();
}

// PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — cache-pause progress update.
// In-place text refresh only; no re-fade. Runs at the sidecar's 2 Hz
// emission cadence (500 ms per call), which is fine for paint — Qt
// coalesces update() requests in a single event-loop pass.
void LoadingOverlay::setCacheProgress(qint64 bytesAhead,
                                      qint64 inputRateBps,
                                      double etaResumeSec,
                                      double cacheDurationSec)
{
    m_cacheValid        = true;
    m_cacheBytesAhead   = bytesAhead;
    m_cacheInputRateBps = inputRateBps;
    m_cacheEtaResumeSec = etaResumeSec;
    m_cacheDurationSec  = cacheDurationSec;
    // Only repaint if we're currently showing the Buffering stage — no
    // point refreshing the overlay mid-open. The bufferingStarted signal
    // must have already run showBuffering() → setStage(Buffering) before
    // this can do work, so m_visible should be true in the happy path.
    if (m_visible && m_stage == Stage::Buffering) {
        update();
    }
}

void LoadingOverlay::clearCacheProgress()
{
    m_cacheValid        = false;
    m_cacheBytesAhead   = 0;
    m_cacheInputRateBps = 0;
    m_cacheEtaResumeSec = -1.0;
    m_cacheDurationSec  = -1.0;
}

// STREAM_STALL_UX_FIX Batch 2 — stream-engine stall diagnostic. Sibling of
// setCacheProgress. In-place text refresh only; no re-fade. Runs at
// StreamPage progressUpdated cadence (~1 Hz) while StreamEngineStats.stalled
// is true. Only repaints when currently in Stage::Buffering — VideoPlayer's
// setStreamStalled handles the fade-in show transition; this call only
// enriches the text after the overlay is already up.
void LoadingOverlay::setStallDiagnostic(int piece, int peerHaveCount)
{
    const bool pieceChanged = (piece != m_stallPiece);
    const bool firstCall    = !m_stallValid;
    m_stallValid         = true;
    m_stallPiece         = piece;
    m_stallPeerHaveCount = peerHaveCount;
    if (firstCall || pieceChanged) {
        logStallDbg(QString("setStallDiagnostic piece=%1 peer_have=%2 visible=%3 stage=%4 first_call=%5")
                        .arg(piece).arg(peerHaveCount).arg(m_visible)
                        .arg(static_cast<int>(m_stage)).arg(firstCall));
    }
    if (m_visible && m_stage == Stage::Buffering) {
        update();
    }
}

void LoadingOverlay::clearStallDiagnostic()
{
    m_stallValid         = false;
    m_stallPiece         = -1;
    m_stallPeerHaveCount = -1;
}

void LoadingOverlay::fadeIn()
{
    repositionCentered();

    m_fadeAnim->stop();
    if (!isVisible()) {
        setOpacity(0.0);
    }
    show();
    raise();

    m_fadeAnim->setDuration(200);
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(1.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    m_fadeAnim->start();
    // Kick the indeterminate sweep on every fadeIn. Idempotent — start()
    // on an already-running QPropertyAnimation resets+continues.
    if (m_phaseAnim && m_phaseAnim->state() != QAbstractAnimation::Running) {
        m_phaseAnim->start();
    }
    update();
}

void LoadingOverlay::fadeOut()
{
    m_fadeAnim->stop();
    m_fadeAnim->setDuration(200);
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(0.0);
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacity <= 0.01) {
            hide();
            // Stop the sweep timer so it doesn't tick forever after dismiss.
            if (m_phaseAnim) m_phaseAnim->stop();
        }
    });
    m_fadeAnim->start();
}

void LoadingOverlay::repositionCentered()
{
    if (!parentWidget()) return;
    const int px = (parentWidget()->width()  - width())  / 2;
    const int py = (parentWidget()->height() - height()) / 2;
    move(px, py);
}

void LoadingOverlay::setOpacity(qreal o)
{
    m_opacity = o;
    update();
}

void LoadingOverlay::setPhase(qreal p)
{
    m_phase = p;
    update();
}

// STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2.1 — per-stage text mapping. Proposal
// A (bracketed-progress, precise, matches sidecar vocabulary). Flip to
// Proposal B (user-literal smoother wording — "Connecting…" / "Loading…"
// / "Almost ready…" / "Almost ready…" / "Buffering…" / "Still working —
// close to retry") at Hemanth's smoke via Rule 14 product decision if
// preferred. Filename appended via em-dash on Opening stage only; sub-
// stages replace the filename line with the stage text (filename retained
// in m_filename for TakingLonger → recovery paint continuity).
QString LoadingOverlay::textForStage() const
{
    switch (m_stage) {
    case Stage::Opening:
        return m_filename.isEmpty()
            ? QStringLiteral("Opening source\u2026")
            : QStringLiteral("Opening source \u2014 ") + m_filename;
    case Stage::Probing:
        return QStringLiteral("Probing source\u2026");
    case Stage::OpeningDecoder:
        return QStringLiteral("Opening decoder\u2026");
    case Stage::DecodingFirstFrame:
        return QStringLiteral("Decoding first frame\u2026");
    case Stage::Buffering: {
        // STREAM_STALL_UX_FIX Batch 2 — stream-engine stall diagnostic wins
        // over sidecar cache-progress. Swarm state is more actionable to the
        // user than the sidecar's internal ring percentage; the "frozen
        // screen" scenario Hemanth reported is specifically the case where
        // libtorrent is waiting for a piece but the sidecar ring hasn't
        // drained yet (prefetch absorbs short waits) -- stream-engine stall
        // source fires before / without the cache-progress source.
        if (m_stallValid) {
            if (m_stallPeerHaveCount > 0) {
                return QStringLiteral("Buffering — waiting for piece %1 (%2 peers have it)")
                           .arg(m_stallPiece)
                           .arg(m_stallPeerHaveCount);
            } else if (m_stallPeerHaveCount == 0) {
                return QStringLiteral("Buffering — waiting for piece %1 (no peers have it yet)")
                           .arg(m_stallPiece);
            } else {
                return QStringLiteral("Buffering — waiting for piece %1")
                           .arg(m_stallPiece);
            }
        }
        // PLAYER_STREMIO_PARITY Phase 2 Batch 2.2 — enriched Buffering text.
        // Fallback to the plain pill when no cache_state has arrived yet
        // (startup-of-stall window) OR when sentinel-unknown (honest).
        if (!m_cacheValid) {
            return QStringLiteral("Buffering\u2026");
        }
        // Resume threshold matches sidecar video_decoder.cpp constant:
        // 1 MiB forward buffer. Percent clamped to [0, 99] — never show
        // 100% because at 100% the sidecar should have already emitted
        // `playing` (stall cleared) which dismisses the overlay.
        constexpr qint64 kResumeThresholdBytes = 1 * 1024 * 1024;
        const qint64 rawPct = m_cacheBytesAhead * 100 / kResumeThresholdBytes;
        const int pct = static_cast<int>(
            qBound<qint64>(qint64{0}, rawPct, qint64{99}));
        // eta_resume_sec == -1.0 → rate unmeasurable; render honestly.
        // Rounded to int seconds; if < 1 s we show "<1s" so the number
        // doesn't freeze at "0s" while work is clearly still ongoing.
        QString tail;
        if (m_cacheEtaResumeSec < 0.0) {
            tail = QStringLiteral("time unknown");
        } else if (m_cacheEtaResumeSec < 1.0) {
            tail = QStringLiteral("resumes in <1s");
        } else {
            tail = QStringLiteral("resumes in ~%1s")
                       .arg(static_cast<int>(m_cacheEtaResumeSec + 0.5));
        }
        return QStringLiteral("Buffering \u2014 %1%% (%2)")
                   .arg(pct)
                   .arg(tail);
    }
    case Stage::TakingLonger:
        return QStringLiteral("Taking longer than expected \u2014 close to retry");
    }
    return {};
}

void LoadingOverlay::paintEvent(QPaintEvent*)
{
    if (m_opacity <= 0.01 || !m_visible) return;

    if (m_stage == Stage::Buffering && m_stallValid && !m_stallPaintLogged) {
        m_stallPaintLogged = true;
        logStallDbg(QString("paintEvent Buffering+stall piece=%1 peer_have=%2 opacity=%3 parent_visible=%4")
                        .arg(m_stallPiece).arg(m_stallPeerHaveCount).arg(m_opacity)
                        .arg(parentWidget() && parentWidget()->isVisible() ? 1 : 0));
    }

    QPainter p(this);
    p.setOpacity(m_opacity);
    p.setRenderHint(QPainter::Antialiasing);

    // Wake 2026-04-24 — simple indeterminate progress bar per Hemanth
    // directive. Two layers: subtle track (white @ 30 alpha) + sweeping
    // band (off-white @ 220 alpha) clipped inside the track. Band is
    // ~30% of width, animated across [-bandW, width()+bandW] via m_phase
    // so it enters smoothly from the left, crosses the track, and exits
    // to the right before the next loop. No text; no stage mapping —
    // every caller (showLoading / showBuffering / setStage) produces
    // the same visual.
    QPainterPath track;
    track.addRoundedRect(QRectF(0, 0, width(), height()), height() / 2.0, height() / 2.0);
    p.fillPath(track, QColor(255, 255, 255, 30));

    const qreal bandW = width() * 0.30;
    const qreal x     = -bandW + m_phase * (static_cast<qreal>(width()) + bandW);

    p.setClipPath(track);
    QPainterPath sweep;
    sweep.addRoundedRect(QRectF(x, 0, bandW, height()), height() / 2.0, height() / 2.0);
    p.fillPath(sweep, QColor(245, 245, 245, 220));
}
