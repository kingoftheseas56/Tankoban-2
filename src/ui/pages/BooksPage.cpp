#include "BooksPage.h"
#include "TileStrip.h"
#include "TileCard.h"
#include "BookSeriesView.h"
#include "core/CoreBridge.h"
#include "core/BooksScanner.h"
#include "core/ScannerUtils.h"

#include "ui/ContextMenuHelper.h"
#include "ui/MainWindow.h"
#include "ui/readers/BookBridge.h"
#include "ui/readers/BookReader.h"
#include "ui/widgets/FadingStackedWidget.h"
#include "ui/widgets/LibraryListView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QMetaObject>
#include <QSettings>
#include <QInputDialog>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QShortcut>
#include <QPushButton>
#include <QRegularExpression>
#include <QFile>
#include <QMessageBox>

BooksPage::BooksPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setObjectName("books");
    qRegisterMetaType<BookSeriesInfo>("BookSeriesInfo");
    qRegisterMetaType<QList<BookSeriesInfo>>("QList<BookSeriesInfo>");

    buildUI();

    m_scanThread = new QThread(this);
    m_scanner = new BooksScanner(m_bridge->dataDir() + "/thumbs");
    m_scanner->moveToThread(m_scanThread);

    connect(m_scanner, &BooksScanner::bookSeriesFound,
            this, &BooksPage::onBookSeriesFound, Qt::QueuedConnection);
    connect(m_scanner, &BooksScanner::scanFinished,
            this, &BooksPage::onScanFinished, Qt::QueuedConnection);

    // REPO_HYGIENE Phase 4 P4.2 (2026-04-26) — race-safe scanner ownership.
    connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);

    m_scanThread->start();

    connect(m_bridge, &CoreBridge::rootFoldersChanged, this, [this](const QString& domain) {
        if (domain == "books")
            triggerScan();
    });
}

BooksPage::~BooksPage()
{
    m_scanThread->quit();
    m_scanThread->wait();
    // REPO_HYGIENE Phase 4 P4.2: m_scanner auto-deleted via deleteLater on
    // thread::finished. No manual delete.
}

// v1.3 Phase D.1 (2026-05-19) — books-side dispatch layer. See
// docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md for
// the 21-command catalog. JS-resident playback commands (seek-page,
// set-layout, get-chapters, open-chapter, tts-play / tts-pause /
// tts-resume / tts-set-voice / tts-set-speed / tts-stop / get-listen-state)
// return a structured `code=JS_RESIDENT_NOT_IMPLEMENTED` reply that
// names the JS file owning the state. Wiring those into BookBridge is a
// follow-on v1.3.x ticket.
namespace {

inline bool replyOk(QJsonObject& reply, QJsonObject fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it)
        reply.insert(it.key(), it.value());
    return true;
}

inline bool replyErr(QJsonObject& reply, const char* code, const QString& msg)
{
    reply["type"]    = QStringLiteral("error");
    reply["code"]    = QString::fromLatin1(code);
    reply["message"] = msg;
    return true;
}

inline bool replyJsResident(QJsonObject& reply, const QString& jsFile,
                            const QString& note)
{
    reply["ok"]         = false;
    reply["code"]       = QStringLiteral("JS_RESIDENT_NOT_IMPLEMENTED");
    reply["jsSource"]   = jsFile;
    reply["note"]       = note;
    return true;
}

inline QString progressKeyFor(const QString& absPath)
{
    return QString(QCryptographicHash::hash(
        absPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

}  // namespace

bool BooksPage::dispatchDevCommand(const QString& cmd,
                                   const QJsonObject& payload,
                                   QJsonObject& reply)
{
    // ── Library + page-local state ────────────────────────────────────────
    if (cmd == QLatin1String("books_get_state"))
        return replyOk(reply, {{"snapshot", devSnapshot()}});

    if (cmd == QLatin1String("books_get_library"))
        return replyOk(reply, {{"library", devLibrarySnapshot()}});

    if (cmd == QLatin1String("books_refresh_library")) {
        const bool wasScanning = m_scanning;
        triggerScan();
        return replyOk(reply, {
            {"triggered",   true},
            {"wasScanning", wasScanning},
            {"scanning",    m_scanning},
            {"buffered",    m_rescanPending}
        });
    }

    if (cmd == QLatin1String("books_search_library")) {
        const QString query = payload.value("query").toString();
        if (!m_searchBar)
            return replyErr(reply, "INTERNAL", "search bar not constructed");
        m_searchBar->setText(query);
        // applySearch is debounce-fired by the QLineEdit textChanged signal,
        // but we want a synchronous reply — run the search immediately.
        applySearch();
        return replyOk(reply, {
            {"query",          query},
            {"visibleSeries",  m_bookStrip ? m_bookStrip->visibleCount() : 0},
            {"bookHitsShown",  m_bookHitsSection && m_bookHitsSection->isVisible()},
            {"totalSeries",    m_bookStrip ? m_bookStrip->totalCount() : 0}
        });
    }

    if (cmd == QLatin1String("books_clear_search")) {
        if (!m_searchBar)
            return replyErr(reply, "INTERNAL", "search bar not constructed");
        m_searchBar->clear();
        applySearch();
        return replyOk(reply, {
            {"visibleSeries", m_bookStrip ? m_bookStrip->visibleCount() : 0},
            {"totalSeries",   m_bookStrip ? m_bookStrip->totalCount() : 0}
        });
    }

    if (cmd == QLatin1String("books_set_sort")) {
        const QString key = payload.value("key").toString();
        if (key.isEmpty())
            return replyErr(reply, "BAD_REQUEST", "payload.key required");
        if (!m_sortCombo)
            return replyErr(reply, "INTERNAL", "sort combo not constructed");
        bool found = false;
        for (int i = 0; i < m_sortCombo->count(); ++i) {
            if (m_sortCombo->itemData(i).toString() == key) {
                m_sortCombo->setCurrentIndex(i);
                found = true;
                break;
            }
        }
        if (!found)
            return replyErr(reply, "BAD_REQUEST",
                QStringLiteral("unknown sort key '%1'").arg(key));
        return replyOk(reply, {{"sortKey", key}});
    }

    if (cmd == QLatin1String("books_set_density")) {
        if (!payload.contains(QStringLiteral("value")))
            return replyErr(reply, "BAD_REQUEST", "payload.value required (0|1|2)");
        const int val = payload.value("value").toInt(-1);
        if (val < 0 || val > 2)
            return replyErr(reply, "BAD_REQUEST", "value must be 0, 1, or 2");
        if (!m_densitySlider)
            return replyErr(reply, "INTERNAL", "density slider not constructed");
        m_densitySlider->setValue(val);
        return replyOk(reply, {{"density", val}});
    }

    if (cmd == QLatin1String("books_open_book")) {
        const QString path = payload.value("path").toString();
        if (path.isEmpty())
            return replyErr(reply, "BAD_REQUEST", "payload.path required");
        if (!QFileInfo::exists(path))
            return replyErr(reply, "BAD_REQUEST",
                QStringLiteral("file does not exist: %1").arg(path));
        emit openBook(path);
        return replyOk(reply, {{"opened", true}, {"path", path}});
    }

    if (cmd == QLatin1String("books_open_series")) {
        const QString seriesPath = payload.value("seriesPath").toString();
        const QString title      = payload.value("title").toString();
        if (seriesPath.isEmpty() && title.isEmpty())
            return replyErr(reply, "BAD_REQUEST",
                "payload.seriesPath or payload.title required");
        QString resolvedPath = seriesPath;
        QString resolvedName;
        if (!resolvedPath.isEmpty()) {
            resolvedName = ScannerUtils::cleanMediaFolderTitle(
                QDir(resolvedPath).dirName());
        } else {
            // Fallback: match by cleaned series name (case-insensitive).
            const QString want = title.trimmed().toLower();
            for (auto it = m_seriesFiles.constBegin();
                 it != m_seriesFiles.constEnd(); ++it) {
                const QString name = ScannerUtils::cleanMediaFolderTitle(
                    QDir(it.key()).dirName());
                if (name.toLower() == want) {
                    resolvedPath = it.key();
                    resolvedName = name;
                    break;
                }
            }
            if (resolvedPath.isEmpty())
                return replyErr(reply, "NOT_FOUND",
                    QStringLiteral("no series matching title '%1'").arg(title));
        }
        if (!m_seriesView || !m_stack)
            return replyErr(reply, "INTERNAL", "series view not constructed");
        m_seriesView->showSeries(resolvedPath, resolvedName);
        m_stack->setCurrentIndexAnimated(1);
        return replyOk(reply, {
            {"seriesPath", resolvedPath},
            {"seriesName", resolvedName}
        });
    }

    if (cmd == QLatin1String("books_get_series_state")) {
        if (!m_seriesView || !m_stack)
            return replyErr(reply, "INTERNAL", "series view not constructed");
        const bool onSeriesView = m_stack->currentIndex() == 1;
        QJsonObject snap = m_seriesView->devSnapshot();
        snap["isVisible"] = onSeriesView;
        return replyOk(reply, {{"snapshot", snap}});
    }

    // ── Reader-side state (needs MainWindow + m_bookReader) ───────────────
    auto* mainWin = qobject_cast<MainWindow*>(window());
    BookReader* reader = mainWin ? mainWin->bookReader() : nullptr;

    if (cmd == QLatin1String("books_get_progress")) {
        if (!reader)
            return replyErr(reply, "NO_READER",
                "BookReader not yet constructed (no book opened this session)");
        QJsonObject readerSnap = reader->devSnapshot();
        QString currentFile = readerSnap.value("currentFile").toString();
        QJsonObject prog;
        if (!currentFile.isEmpty() && m_bridge) {
            prog = m_bridge->progress("books", progressKeyFor(currentFile));
        }
        return replyOk(reply, {
            {"reader",      readerSnap},
            {"progressKey", currentFile.isEmpty() ? QString()
                                : progressKeyFor(currentFile)},
            {"progress",    prog}
        });
    }

    // JS-resident: page/layout/chapter live in engine_foliate.js.
    if (cmd == QLatin1String("books_seek_page")
        || cmd == QLatin1String("books_set_layout")
        || cmd == QLatin1String("books_get_chapters")
        || cmd == QLatin1String("books_open_chapter")) {
        if (!reader)
            return replyErr(reply, "NO_READER",
                "BookReader not yet constructed (no book opened this session)");
        return replyJsResident(reply,
            QStringLiteral("src/ui/readers/engine_foliate.js"),
            QStringLiteral("reader page/layout/chapter state lives in Foliate's "
                           "JS engine; v1.3.x will wire BookBridge methods to "
                           "drive these via runJavaScript"));
    }

    // ── TTS-side state (BookBridge holds the Qt-side worker) ──────────────
#ifdef HAS_WEBENGINE
    BookBridge* bridge = reader ? reader->bridge() : nullptr;

    if (cmd == QLatin1String("books_tts_state")) {
        if (!bridge)
            return replyErr(reply, "NO_READER",
                "BookBridge not yet constructed (no book opened this session)");
        QJsonObject ttsSnap = bridge->devTtsSnapshot();
        ttsSnap["jsPlaybackNote"] = QStringLiteral(
            "playing/paused/voice/speed/position live in tts_core.js");
        return replyOk(reply, {{"snapshot", ttsSnap}});
    }

    if (cmd == QLatin1String("books_tts_cancel_stream")) {
        if (!bridge)
            return replyErr(reply, "NO_READER",
                "BookBridge not yet constructed (no book opened this session)");
        if (!payload.contains(QStringLiteral("streamId")))
            return replyErr(reply, "BAD_REQUEST", "payload.streamId required");
        const quint64 streamId = static_cast<quint64>(
            payload.value("streamId").toVariant().toULongLong());
        bridge->devCancelStream(streamId);
        return replyOk(reply, {{"cancelled", true}, {"streamId",
            static_cast<double>(streamId)}});
    }
#else
    if (cmd == QLatin1String("books_tts_state")
        || cmd == QLatin1String("books_tts_cancel_stream")) {
        return replyErr(reply, "WEBENGINE_DISABLED",
            "Edge TTS surface requires HAS_WEBENGINE build");
    }
#endif

    // JS-resident: playback control lives in tts_core.js + engine_foliate.js.
    if (cmd == QLatin1String("books_tts_play")
        || cmd == QLatin1String("books_tts_pause")
        || cmd == QLatin1String("books_tts_resume")
        || cmd == QLatin1String("books_tts_stop")
        || cmd == QLatin1String("books_tts_set_voice")
        || cmd == QLatin1String("books_tts_set_speed")
        || cmd == QLatin1String("books_get_listen_state")) {
        if (!reader)
            return replyErr(reply, "NO_READER",
                "BookReader not yet constructed (no book opened this session)");
        return replyJsResident(reply,
            QStringLiteral("src/ui/readers/tts_core.js"),
            QStringLiteral("TTS playback / voice / speed / Listen-button state "
                           "lives in the JS layer; v1.3.x will add BookBridge "
                           "Q_INVOKABLE methods + runJavaScript drivers"));
    }

    // ── v1.6 library-side bridge (Phase D.4, 2026-05-19) ──────────────────
    if (cmd.startsWith(QLatin1String("library_"))) {
        if (cmd == QLatin1String("library_get_section"))
            return replyOk(reply, {{"section", devLibrarySection()}});
        if (cmd == QLatin1String("library_get_continue_reading")) {
            const QJsonObject sec = devLibrarySection();
            return replyOk(reply, {{"cr_strip", sec.value("cr_strip").toObject()}});
        }
        if (cmd == QLatin1String("library_get_recently_added")) {
            const QJsonObject sec = devLibrarySection();
            return replyOk(reply,
                {{"recently_added", sec.value("recently_added").toObject()}});
        }
        if (cmd == QLatin1String("library_get_search_state")) {
            return replyOk(reply, {
                {"query", m_searchBar ? m_searchBar->text() : QString()},
                {"search_state", devLibrarySection().value("search_state").toObject()}
            });
        }
        if (cmd == QLatin1String("library_get_scan_state")) {
            return replyOk(reply, {
                {"scan_state", devLibrarySection().value("scan_state").toObject()}
            });
        }
        if (cmd == QLatin1String("library_trigger_scan")) {
            const bool was = m_scanning;
            triggerScan();
            return replyOk(reply, {{"triggered", true},
                                   {"wasScanning", was},
                                   {"scanning", m_scanning},
                                   {"buffered", m_rescanPending}});
        }
        if (cmd == QLatin1String("library_get_sort"))
            return replyOk(reply, {{"sortKey",
                m_sortCombo ? m_sortCombo->currentData().toString() : QString()}});
        if (cmd == QLatin1String("library_set_sort")) {
            const QString key = payload.value("key").toString();
            if (key.isEmpty())
                return replyErr(reply, "BAD_REQUEST", "payload.key required");
            if (!m_sortCombo)
                return replyErr(reply, "INTERNAL", "sort combo not constructed");
            for (int i = 0; i < m_sortCombo->count(); ++i) {
                if (m_sortCombo->itemData(i).toString() == key) {
                    m_sortCombo->setCurrentIndex(i);
                    return replyOk(reply, {{"sortKey", key}});
                }
            }
            return replyErr(reply, "BAD_REQUEST",
                QStringLiteral("unknown sort key '%1'").arg(key));
        }
        if (cmd == QLatin1String("library_set_density")) {
            const int val = payload.value("value").toInt(-1);
            if (val < 0 || val > 2)
                return replyErr(reply, "BAD_REQUEST", "value must be 0|1|2");
            if (!m_densitySlider)
                return replyErr(reply, "INTERNAL", "density slider not constructed");
            m_densitySlider->setValue(val);
            return replyOk(reply, {{"density", val}});
        }
        if (cmd == QLatin1String("library_set_search_query")) {
            if (!m_searchBar)
                return replyErr(reply, "INTERNAL", "search bar not constructed");
            const QString q = payload.value("query").toString();
            m_searchBar->setText(q);
            applySearch();
            return replyOk(reply, {{"query", q}});
        }
        if (cmd == QLatin1String("library_get_active_layer"))
            return replyOk(reply, {{"layer",
                devLibrarySection().value("active_layer").toString()}});
        if (cmd == QLatin1String("library_reset_mode")) {
            // BooksPage has no resetToRoot slot; showGrid flips m_stack
            // back to index 0 (library grid), matching the contract from
            // feedback_mode_pill_resets_to_root.md for non-deep modes.
            showGrid();
            return replyOk(reply, {{"reset", true},
                {"layer", devLibrarySection().value("active_layer").toString()}});
        }
        if (cmd == QLatin1String("library_get_selected_items"))
            return replyOk(reply, {{"selection", QJsonArray{}}});
        return false;  // unknown library_* — fall through to UNKNOWN_CMD
    }

    return false;  // unknown books_* command — fall through to UNKNOWN_CMD
}

// v1.6 Phase D.4 (2026-05-19) — cross-mode library-section snapshot.
QJsonObject BooksPage::devLibrarySection() const
{
    QJsonObject sec;
    QJsonObject cr;
    cr["visible"] = m_continueSection && m_continueSection->isVisible();
    cr["count"]   = static_cast<int>(m_progressKeyMap.size());
    sec["cr_strip"] = cr;

    QJsonObject ra;
    ra["count"]   = m_bookStrip ? m_bookStrip->totalCount() : 0;
    ra["visible"] = m_bookStrip && m_bookStrip->totalCount() > 0;
    sec["recently_added"] = ra;

    QJsonObject ss;
    ss["query"]         = m_searchBar ? m_searchBar->text() : QString();
    ss["visibleSeries"] = m_bookStrip ? m_bookStrip->visibleCount() : 0;
    sec["search_state"] = ss;

    QJsonObject scan;
    scan["scanning"]      = m_scanning;
    scan["hasScanned"]    = m_hasScanned;
    scan["rescanPending"] = m_rescanPending;
    sec["scan_state"] = scan;

    QJsonArray roots;
    if (m_bridge)
        for (const QString& p : m_bridge->rootFolders(QStringLiteral("books")))
            roots.append(p);
    sec["root_folders"] = roots;

    sec["sort_key"] = m_sortCombo ? m_sortCombo->currentData().toString() : QString();
    sec["density"]  = m_densitySlider ? m_densitySlider->value() : -1;
    sec["selection"] = QJsonArray{};
    sec["active_layer"] = (m_stack && m_stack->currentIndex() == 1)
        ? QStringLiteral("series-view") : QStringLiteral("library");
    return sec;
}

// v1.3 Phase D.1 (2026-05-19) — page-local snapshot for books-get-state +
// dump-ui books.
QJsonObject BooksPage::devSnapshot() const
{
    QJsonObject snap;
    snap["activePageId"]   = QStringLiteral("books");
    snap["hasScanned"]     = m_hasScanned;
    snap["scanning"]       = m_scanning;
    snap["rescanPending"]  = m_rescanPending;
    snap["gridMode"]       = m_gridMode;
    snap["seriesCount"]    = m_seriesFiles.size();
    snap["progressEntries"] = m_progressKeyMap.size();
    snap["searchText"]     = m_searchBar ? m_searchBar->text() : QString();
    snap["sortKey"]        = m_sortCombo ? m_sortCombo->currentData().toString()
                                         : QString();
    snap["density"]        = m_densitySlider ? m_densitySlider->value() : -1;
    snap["seriesViewActive"] = m_stack && m_stack->currentIndex() == 1;
    snap["library"] = devLibrarySection();

    if (auto* mainWin = qobject_cast<const MainWindow*>(
            const_cast<BooksPage*>(this)->window())) {
        BookReader* reader = mainWin->bookReader();
        QJsonObject readerSnap;
        readerSnap["constructed"] = reader != nullptr;
        readerSnap["isVisible"]   = reader && reader->isVisible();
        if (reader)
            readerSnap["currentFile"] = reader->devSnapshot()
                                            .value("currentFile").toString();
        snap["reader"] = readerSnap;
    }
    return snap;
}

// v1.3 Phase D.1 (2026-05-19) — library snapshot (one entry per series
// folder + the file roster collected during the last scan).
QJsonObject BooksPage::devLibrarySnapshot() const
{
    QJsonArray entries;
    for (auto it = m_seriesFiles.constBegin(); it != m_seriesFiles.constEnd();
         ++it) {
        QJsonObject e;
        const QString seriesPath = it.key();
        e["seriesPath"] = seriesPath;
        e["seriesName"] = ScannerUtils::cleanMediaFolderTitle(
            QDir(seriesPath).dirName());
        e["fileCount"]  = it.value().size();
        QJsonArray files;
        for (const auto& bf : it.value()) {
            QJsonObject f;
            f["title"]    = bf.title;
            f["filePath"] = bf.filePath;
            f["progressKey"] = progressKeyFor(bf.filePath);
            files.append(f);
        }
        e["files"] = files;
        entries.append(e);
    }
    QJsonObject snap;
    snap["entries"] = entries;
    snap["count"]   = entries.size();
    snap["hasScanned"] = m_hasScanned;
    snap["scanning"]   = m_scanning;
    return snap;
}

void BooksPage::buildUI()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_stack = new FadingStackedWidget(this);

    // ── Grid view (index 0) ──
    auto* gridPage = new QWidget();
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    // Scrollable content area
    auto* scroll = new QScrollArea(gridPage);
    m_gridScroll = scroll;  // GLOBAL_NAV_HISTORY Task 9
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background: transparent;");

    auto* content = new QWidget();
    content->setStyleSheet("background: transparent;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 0, 20, 20);
    layout->setSpacing(24);

    // ── 1. Search bar (full width, top) ──
    m_searchBar = new QLineEdit(content);
    m_searchBar->setPlaceholderText("Search books and series\u2026");
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setObjectName("LibrarySearch");
    m_searchBar->setFixedHeight(36);
    m_searchBar->setStyleSheet(
        "QLineEdit#LibrarySearch { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 4px 10px; font-size: 13px; }"
        "QLineEdit#LibrarySearch:focus { border: 1px solid rgba(255,255,255,0.3); }");
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 12, 0, 0);
    searchLayout->addWidget(m_searchBar);
    layout->addLayout(searchLayout);

    m_searchBar->setToolTip("Separate words to match all\n"
                            "(e.g. 'one piece' matches series or volumes containing both words)");

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() {
        m_searchTimer->start();
        m_searchBar->setProperty("activeSearch", !m_searchBar->text().trimmed().isEmpty());
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);
    });
    connect(m_searchTimer, &QTimer::timeout, this, &BooksPage::applySearch);

    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchBar->setFocus();
        m_searchBar->selectAll();
    });

    // ── 2. Continue Reading section ──
    m_continueSection = new QWidget(content);
    auto* continueLayout = new QVBoxLayout(m_continueSection);
    continueLayout->setContentsMargins(0, 0, 0, 0);
    continueLayout->setSpacing(4);
    auto* continueLabel = new QLabel("CONTINUE READING", m_continueSection);
    continueLabel->setObjectName("LibraryHeading");
    continueLayout->addWidget(continueLabel);
    m_continueStrip = new TileStrip(m_continueSection);
    m_continueStrip->setMode("continue");
    continueLayout->addWidget(m_continueStrip);

    // Continue-tile context menu
    m_continueStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_continueStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_continueStrip->tileAt(pos);
        if (!card) return;

        QString filePath = card->property("filePath").toString();
        QString seriesPath = card->property("seriesPath").toString();
        QString seriesName = card->property("seriesName").toString();
        QString coverPath = card->property("coverPath").toString();
        QString progKey = card->property("progressKey").toString();

        bool isFinished = false;
        if (m_bridge && !progKey.isEmpty()) {
            QJsonObject prog = m_bridge->progress("books", progKey);
            isFinished = prog.value("finished").toBool();
        }

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* continueAct = menu->addAction("Continue reading");
        auto* openSeriesAct = menu->addAction("Open series");
        openSeriesAct->setEnabled(!seriesPath.isEmpty());
        menu->addSeparator();
        auto* markAct = menu->addAction(isFinished ? "Mark as unread" : "Mark as read");
        auto* clearAct = menu->addAction("Clear from Continue Reading");
        menu->addSeparator();
        auto* renameAct = menu->addAction("Rename...");
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!filePath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!filePath.isEmpty());
        menu->addSeparator();
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove from library...");
        removeAct->setEnabled(!seriesPath.isEmpty());

        auto* chosen = menu->exec(m_continueStrip->mapToGlobal(pos));
        if (chosen == continueAct) {
            if (!filePath.isEmpty()) emit openBook(filePath);
        } else if (chosen == openSeriesAct) {
            m_seriesView->showSeries(seriesPath, seriesName, coverPath);
            m_stack->setCurrentIndexAnimated(1);
        } else if (chosen == markAct) {
            if (m_bridge && !progKey.isEmpty()) {
                QJsonObject prog = m_bridge->progress("books", progKey);
                prog["finished"] = !isFinished;
                m_bridge->saveProgress("books", progKey, prog);
                refreshContinueStrip();
            }
        } else if (chosen == clearAct) {
            if (m_bridge && !progKey.isEmpty()) {
                m_bridge->clearProgress("books", progKey);
                refreshContinueStrip();
            }
        } else if (chosen == renameAct) {
            if (!filePath.isEmpty()) {
                QFileInfo fi(filePath);
                QString newName = QInputDialog::getText(this, "Rename",
                    "New name:", QLineEdit::Normal, fi.completeBaseName());
                if (!newName.isEmpty() && newName != fi.completeBaseName()) {
                    QString newPath = fi.absolutePath() + "/" + newName + "." + fi.suffix();
                    if (QFile::rename(filePath, newPath)) {
                        refreshContinueStrip();
                    } else {
                        QMessageBox::warning(this, "Rename failed",
                            "Could not rename \"" + fi.fileName() + "\".\n"
                            "The file may be in use by another program.");
                    }
                }
            }
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(filePath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(filePath);
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this series from the library?\n" + seriesPath +
                    "\n\nFiles will remain on disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    m_continueSection->hide();
    layout->addWidget(m_continueSection);

    // ── 3. "BOOKS" header row: label + sort + density ──
    auto* booksRow = new QWidget(content);
    auto* booksRowLayout = new QHBoxLayout(booksRow);
    booksRowLayout->setContentsMargins(0, 0, 0, 0);
    booksRowLayout->setSpacing(8);

    auto* booksLabel = new QLabel("BOOKS", booksRow);
    booksLabel->setObjectName("LibraryHeading");
    booksRowLayout->addWidget(booksLabel);
    booksRowLayout->addStretch();

    m_sortCombo = new QComboBox(booksRow);
    m_sortCombo->setObjectName("LibrarySortCombo");
    m_sortCombo->setFixedWidth(150);
    m_sortCombo->setFixedHeight(28);
    m_sortCombo->addItem("Name A\u2192Z",       "name_asc");
    m_sortCombo->addItem("Name Z\u2192A",       "name_desc");
    m_sortCombo->addItem("Recently updated",     "updated_desc");
    m_sortCombo->addItem("Least recent",         "updated_asc");
    m_sortCombo->addItem("Most items",           "count_desc");
    m_sortCombo->addItem("Fewest items",         "count_asc");
    m_sortCombo->setStyleSheet(
        "QComboBox#LibrarySortCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#LibrarySortCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#LibrarySortCombo::drop-down { border: none; }"
        "QComboBox#LibrarySortCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_books", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_books", key);
        m_bookStrip->sortTiles(key);
    });
    booksRowLayout->addWidget(m_sortCombo);

    auto* densitySmall = new QLabel("A", booksRow);
    densitySmall->setObjectName("DensityLabelSmall");
    booksRowLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, booksRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size", val);
        m_bookStrip->setDensity(val);
        if (m_continueStrip) m_continueStrip->setDensity(val);
    });
    booksRowLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", booksRow);
    densityLarge->setObjectName("DensityLabelLarge");
    booksRowLayout->addWidget(densityLarge);

    m_viewToggle = new QPushButton(booksRow);
    m_viewToggle->setObjectName("ViewToggle");
    m_viewToggle->setFixedSize(28, 28);
    m_viewToggle->setCursor(Qt::PointingHandCursor);
    m_viewToggle->setToolTip("Toggle grid/list view (V)");
    m_viewToggle->setStyleSheet(
        "QPushButton#ViewToggle { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 6px; color: rgba(255,255,255,0.5); font-size: 14px; }"
        "QPushButton#ViewToggle:hover { background: rgba(255,255,255,0.1); color: rgba(255,255,255,0.8); }");
    m_viewToggle->setText(QString::fromUtf8("\u2630")); // hamburger icon for grid mode
    booksRowLayout->addWidget(m_viewToggle);

    layout->addWidget(booksRow);

    m_bookStatus = new QLabel("Add a books folder to get started", content);
    m_bookStatus->setObjectName("TileSubtitle");
    m_bookStatus->setAlignment(Qt::AlignCenter);
    m_bookStatus->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 40px;");
    layout->addWidget(m_bookStatus);

    m_bookStrip = new TileStrip(content);
    m_bookStrip->hide();
    m_bookStrip->setMinimumHeight(340);
    layout->addWidget(m_bookStrip);

    // ── List view (hidden by default, shown on V toggle) ──
    m_listView = new LibraryListView(content);
    m_listView->hide();
    layout->addWidget(m_listView);

    connect(m_listView, &LibraryListView::itemActivated, this, [this](const QString& path) {
        // Find the series for this path and open series view
        for (auto it = m_seriesFiles.begin(); it != m_seriesFiles.end(); ++it) {
            if (it.key() == path) {
                QString name = ScannerUtils::cleanMediaFolderTitle(QDir(path).dirName());
                QString hash = QString(QCryptographicHash::hash(
                    path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QString thumbsDir = m_bridge->dataDir() + "/thumbs";
                QString cover = thumbsDir + "/" + hash + ".jpg";
                m_seriesView->showSeries(path, name, QFile::exists(cover) ? cover : QString());
                m_stack->setCurrentIndexAnimated(1);
                return;
            }
        }
    });

    // View toggle logic
    m_gridMode = QSettings("Tankoban", "Tankoban").value("library_view_mode_books", "grid").toString() == "grid";
    connect(m_viewToggle, &QPushButton::clicked, this, [this]() {
        m_gridMode = !m_gridMode;
        QSettings("Tankoban", "Tankoban").setValue("library_view_mode_books", m_gridMode ? "grid" : "list");
        if (m_gridMode) {
            m_listView->hide();
            m_bookStrip->show();
            m_densitySlider->show();
            m_viewToggle->setText(QString::fromUtf8("\u2630")); // hamburger
        } else {
            m_bookStrip->hide();
            m_listView->show();
            m_densitySlider->hide();
            m_viewToggle->setText(QString::fromUtf8("\u2637")); // dotted square
        }
    });

    // Context menu on book tiles
    m_bookStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookStrip, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* card = m_bookStrip->tileAt(pos);
        if (!card) return;

        QString seriesPath = card->property("seriesPath").toString();
        QString seriesName = card->property("seriesName").toString();
        QString coverPath = card->property("coverPath").toString();

        // Check if all books are finished
        static const QStringList bookExts = {"*.epub","*.pdf","*.mobi","*.fb2","*.azw3","*.djvu","*.txt"};
        QDir dir(seriesPath);
        QStringList bookFiles = dir.entryList(bookExts, QDir::Files);
        QJsonObject allProg = m_bridge->allProgress("books");
        bool allFinished = !bookFiles.isEmpty();
        for (const auto& f : bookFiles) {
            QString id = QString(QCryptographicHash::hash(
                dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            if (!allProg.value(id).toObject().value("finished").toBool()) {
                allFinished = false;
                break;
            }
        }

        // Find most recent in-progress book for "Continue reading"
        bool hasInProgress = false;
        QString continueFilePath;
        qint64 bestAt = -1;
        for (const auto& f : bookFiles) {
            QString fullPath = dir.absoluteFilePath(f);
            QString id = QString(QCryptographicHash::hash(
                fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QJsonObject prog = allProg.value(id).toObject();
            if (!prog.isEmpty() && !prog.value("finished").toBool() && prog.value("page").toInt(-1) >= 0) {
                hasInProgress = true;
                qint64 updAt = prog.value("updatedAt").toVariant().toLongLong();
                if (updAt > bestAt) {
                    bestAt = updAt;
                    continueFilePath = fullPath;
                }
            }
        }

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* openAct = menu->addAction("Open series");
        auto* continueAct = menu->addAction("Continue reading");
        continueAct->setEnabled(hasInProgress);
        menu->addSeparator();
        auto* markAct = menu->addAction(allFinished ? "Mark all as unread" : "Mark all as read");
        menu->addSeparator();
        auto* renameAct = menu->addAction("Rename series...");
        auto* hideAct = menu->addAction("Hide series");
        auto* revealAct = menu->addAction("Reveal in File Explorer");
        revealAct->setEnabled(!seriesPath.isEmpty());
        auto* copyAct = menu->addAction("Copy path");
        copyAct->setEnabled(!seriesPath.isEmpty());
        menu->addSeparator();
        auto* removeAct = ContextMenuHelper::addDangerAction(menu, "Remove series folder...");
        removeAct->setEnabled(!seriesPath.isEmpty());

        auto* chosen = menu->exec(m_bookStrip->mapToGlobal(pos));
        if (chosen == openAct) {
            m_seriesView->showSeries(seriesPath, seriesName, coverPath);
            m_stack->setCurrentIndexAnimated(1);
        } else if (chosen == continueAct) {
            if (!continueFilePath.isEmpty())
                emit openBook(continueFilePath);
        } else if (chosen == markAct) {
            bool setFinished = !allFinished;
            for (const auto& f : bookFiles) {
                QString id = QString(QCryptographicHash::hash(
                    dir.absoluteFilePath(f).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QJsonObject prog = m_bridge->progress("books", id);
                prog["finished"] = setFinished;
                m_bridge->saveProgress("books", id, prog);
            }
        } else if (chosen == renameAct) {
            QString dirName = QDir(seriesPath).dirName();
            QString newName = QInputDialog::getText(this, "Rename series", "New name:", QLineEdit::Normal, dirName);
            if (!newName.isEmpty() && newName != dirName) {
                QString parentPath = QFileInfo(seriesPath).absolutePath();
                QString oldPath = parentPath + "/" + dirName;
                QString newPath = parentPath + "/" + newName.trimmed();
                if (QFile::rename(oldPath, newPath)) {
                    triggerScan();
                } else {
                    QMessageBox::warning(this, "Rename failed",
                        "Could not rename \"" + dirName + "\".\n"
                        "The folder may be in use by another program.");
                }
            }
        } else if (chosen == hideAct) {
            QSettings settings("Tankoban", "Tankoban");
            QStringList hidden = settings.value("books_hidden_series").toStringList();
            if (!hidden.contains(seriesPath)) {
                hidden.append(seriesPath);
                settings.setValue("books_hidden_series", hidden);
            }
            card->hide();
            m_bookStrip->filterTiles(m_searchBar->text());
        } else if (chosen == revealAct) {
            ContextMenuHelper::revealInExplorer(seriesPath);
        } else if (chosen == copyAct) {
            ContextMenuHelper::copyToClipboard(seriesPath);
        } else if (chosen == removeAct) {
            if (ContextMenuHelper::confirmRemove(this, "Remove from library",
                    "Remove this series from the library?\n" + seriesPath +
                    "\n\nFiles will remain on disk.")) {
                triggerScan();
            }
        }
        menu->deleteLater();
    });

    // ── Book Hits section (scored search — individual book matches) ──
    m_bookHitsSection = new QWidget(content);
    auto* bhLayout = new QVBoxLayout(m_bookHitsSection);
    bhLayout->setContentsMargins(0, 0, 0, 0);
    bhLayout->setSpacing(0);
    m_bookHitsStrip = new TileStrip(m_bookHitsSection);
    m_bookHitsStrip->setStripLabel("MATCHING BOOKS");
    bhLayout->addWidget(m_bookHitsStrip);
    m_bookHitsSection->hide();
    layout->addWidget(m_bookHitsSection);

    // Apply saved density now that all strips exist. Continue strip density
    // gate was dropped in TileStrip.cpp 2026-04-25 so it now responds to
    // setDensity uniformly.
    m_bookStrip->setDensity(savedDensity);
    if (m_continueStrip) m_continueStrip->setDensity(savedDensity);

    layout->addStretch(1);
    scroll->setWidget(content);
    gridLayout->addWidget(scroll, 1);

    m_stack->addWidget(gridPage);

    // ── Series view (index 1) ──
    m_seriesView = new BookSeriesView(m_bridge);
    connect(m_seriesView, &BookSeriesView::backRequested, this, &BooksPage::showGrid);
    connect(m_seriesView, &BookSeriesView::bookSelected, this, &BooksPage::openBook);
    m_stack->addWidget(m_seriesView);

    outerLayout->addWidget(m_stack, 1);

    // ── Keyboard shortcuts (Batch 7) ──
    // Task 7 (2026-05-01) — scope to widget so it doesn't intercept Esc
    // when BookReader is shown over this page in the QStackedWidget.
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (!m_searchBar->text().trimmed().isEmpty()) {
            m_searchBar->clear();
        } else if (m_stack->currentIndex() == 1) {
            showGrid();
        }
    });

    auto* f5Shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5Shortcut, &QShortcut::activated, this, [this]() { triggerScan(); });

    auto* refreshShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(refreshShortcut, &QShortcut::activated, this, [this]() {
        refreshContinueStrip();
        m_bookStrip->sortTiles(m_sortCombo->currentData().toString());
    });

    auto* vShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(vShortcut, &QShortcut::activated, this, [this]() {
        if (m_searchBar->hasFocus()) return; // no-op if search focused
        m_viewToggle->click();
    });

    auto* selectAllShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), this);
    connect(selectAllShortcut, &QShortcut::activated, this, [this]() {
        if (m_gridMode)
            m_bookStrip->selectAll();
    });

    // Apply initial view mode
    if (!m_gridMode) {
        m_bookStrip->hide();
        m_listView->show();
        m_densitySlider->hide();
        m_viewToggle->setText(QString::fromUtf8("\u2637"));
    }
}

void BooksPage::activate()
{
    if (!m_hasScanned)
        triggerScan();
}

void BooksPage::triggerScan()
{
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — buffer rather than drop.
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    m_scanning = true;
    m_rescanPending = false;

    QStringList bookRoots = m_bridge->rootFolders("books");

    if (bookRoots.isEmpty()) {
        m_bookStrip->clear();
        m_bookStrip->hide();
        m_bookStatus->setText("Add a books folder to get started");
        m_bookStatus->show();
        m_hasScanned = true;
        m_scanning = false;
        return;
    }

    if (!m_hasScanned) {
        // First scan: clear tiles, show scanning label for progressive loading
        m_bookStrip->clear();
        m_bookHitsStrip->clear();
        m_bookHitsSection->hide();
        m_progressKeyMap.clear();
        m_seriesFiles.clear();
        m_bookStatus->setText("Scanning...");
        m_bookStatus->show();
        m_bookStrip->hide();
    }
    // Rescan: keep old tiles visible — atomic swap happens in onScanFinished

    QMetaObject::invokeMethod(m_scanner, "scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, bookRoots));
}

void BooksPage::addBookSeriesTile(const BookSeriesInfo& series)
{
    static const QStringList bookExts = {"*.epub","*.pdf","*.mobi","*.fb2","*.azw3","*.djvu","*.txt"};
    QString thumbsDir = m_bridge->dataDir() + "/thumbs";
    QDir dir(series.seriesPath);
    QList<BookFile> fileList;
    for (const auto& f : dir.entryList(bookExts, QDir::Files)) {
        QString fullPath = dir.absoluteFilePath(f);
        QString progressKey = QString(QCryptographicHash::hash(
            fullPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QFileInfo fi(fullPath);
        QString fileKey = fullPath + "::" + QString::number(fi.size())
                        + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
        QString fileHash = QString(QCryptographicHash::hash(
            fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString fileCover = thumbsDir + "/" + fileHash + ".jpg";
        QString coverPath = QFile::exists(fileCover) ? fileCover : series.coverThumbPath;
        m_progressKeyMap[progressKey] = {fullPath, series.seriesPath, coverPath};
        fileList.append({fullPath, ScannerUtils::cleanMediaFolderTitle(fi.completeBaseName())});
    }
    m_seriesFiles[series.seriesPath] = fileList;

    QString subtitle = QString::number(series.fileCount)
                     + (series.fileCount == 1 ? " book" : " books");

    auto* card = new TileCard(series.coverThumbPath, series.seriesName, subtitle);

    card->setProperty("seriesPath", series.seriesPath);
    card->setProperty("seriesName", series.seriesName);
    card->setProperty("coverPath", series.coverThumbPath);
    card->setProperty("fileCount", series.fileCount);
    card->setProperty("newestMtime", series.newestMtimeMs);
    connect(card, &TileCard::clicked, this, [this, card]() {
        QString path = card->property("seriesPath").toString();
        QString name = card->property("seriesName").toString();
        QString cover = card->property("coverPath").toString();
        m_seriesView->showSeries(path, name, cover);
        m_stack->setCurrentIndexAnimated(1);
    });

    m_bookStrip->addTile(card);
}

void BooksPage::onBookSeriesFound(const BookSeriesInfo& series)
{
    // On rescan: skip incremental tiles — atomic rebuild in onScanFinished
    if (m_hasScanned) return;

    // First scan: progressive loading
    if (m_bookStatus->isVisible()) {
        m_bookStatus->hide();
        m_bookStrip->show();
    }
    addBookSeriesTile(series);
}

void BooksPage::onScanFinished(const QList<BookSeriesInfo>& allBooks)
{
    bool wasRescan = m_hasScanned;
    m_hasScanned = true;
    m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — fire pending rescan.
    if (m_rescanPending) {
        m_rescanPending = false;
        QTimer::singleShot(0, this, [this]() { triggerScan(); });
    }

    if (wasRescan) {
        // Atomic swap: clear old tiles, rebuild from complete list
        m_bookStrip->clear();
        m_bookHitsStrip->clear();
        m_bookHitsSection->hide();
        m_listView->clear();
        m_progressKeyMap.clear();
        m_seriesFiles.clear();

        for (const auto& series : allBooks)
            addBookSeriesTile(series);
    }

    // Populate list view
    m_listView->clear();
    for (const auto& series : allBooks) {
        LibraryListView::ItemData item;
        item.name = series.seriesName;
        item.path = series.seriesPath;
        item.itemCount = series.fileCount;
        item.lastModifiedMs = series.newestMtimeMs;
        m_listView->addItem(item);
    }

    if (allBooks.isEmpty()) {
        m_bookStrip->hide();
        m_listView->hide();
        m_bookStatus->setObjectName("LibraryEmptyLabel");
        m_bookStatus->setAlignment(Qt::AlignCenter);
        m_bookStatus->setText("No books found\nAdd a root folder via the + button or browse Sources for content");
        m_bookStatus->show();
    } else {
        m_bookStatus->hide();
        m_bookStrip->show();
        m_bookStrip->sortTiles(m_sortCombo->currentData().toString());
    }

    refreshContinueStrip();
}

void BooksPage::onTileClicked(const QString& seriesPath, const QString& seriesName)
{
    m_seriesView->showSeries(seriesPath, seriesName);
    m_stack->setCurrentIndexAnimated(1);
}

void BooksPage::showGrid()
{
    m_stack->setCurrentIndexAnimated(0);
}

// ── Scored search helpers ──

static QStringList tokenize(const QString& text)
{
    static QRegularExpression re("[a-z0-9]+");
    QStringList tokens;
    auto it = re.globalMatch(text.toLower());
    while (it.hasNext())
        tokens.append(it.next().captured());
    return tokens;
}

static int scoreTokens(const QString& text, const QStringList& queryTokens, const QString& fullQuery)
{
    QString lower = text.toLower();
    QStringList textTokens = tokenize(text);
    int score = 0;

    for (const auto& qt : queryTokens) {
        bool found = false;
        // Check substring match in full text
        if (lower.contains(qt)) {
            score += 14;
            found = true;
        }
        // Check exact word match
        for (const auto& tt : textTokens) {
            if (tt == qt) { score += 12; found = true; break; }
            if (tt.startsWith(qt)) { score += 6; found = true; break; }
        }
        if (!found) return 0; // AND logic: all tokens must match
    }

    // Full phrase bonus
    if (!fullQuery.isEmpty() && lower.contains(fullQuery.toLower()))
        score += 140;

    return score;
}

void BooksPage::applySearch()
{
    QString rawQuery = m_searchBar->text().trimmed();
    bool searchActive = !rawQuery.isEmpty();

    // Clear book hits from previous search
    m_bookHitsStrip->clear();
    m_bookHitsSection->hide();

    if (!searchActive) {
        // No search — show all, delegate to simple filter
        m_bookStrip->filterTiles(QString());

        if (m_bookStrip->totalCount() > 0) {
            m_bookStatus->hide();
            m_bookStrip->show();
        }
        return;
    }

    QStringList queryTokens = tokenize(rawQuery);
    if (queryTokens.isEmpty()) {
        m_bookStrip->filterTiles(QString());
        return;
    }

    // ── Score each series tile ──
    // We need to manually show/hide + sort by score, rather than using filterTiles
    m_bookStrip->filterTiles(QString()); // first show all
    struct ScoredTile { TileCard* card; int score; };
    QList<ScoredTile> scoredTiles;
    QSet<QString> seriesWithBookHits; // seriesPath of series that got book-level hits

    // Score series names
    for (int i = 0; i < m_bookStrip->totalCount(); ++i) {
        // Access tiles through the strip — use tileAt with position isn't ideal
        // Instead, iterate children
    }

    // Since TileStrip doesn't expose tile iteration directly, use filterTiles
    // and then also do book-level search for the "Book Hits" strip
    m_bookStrip->filterTiles(rawQuery);

    // ── Book-level search: find individual books matching query ──
    int bookHitCount = 0;
    QString thumbsDir = m_bridge->dataDir() + "/thumbs";

    for (auto it = m_seriesFiles.begin(); it != m_seriesFiles.end() && bookHitCount < 24; ++it) {
        QString seriesPath = it.key();
        // Check if the series name already matches — skip book-level hits for matching series
        QString seriesName = QDir(seriesPath).dirName();
        int seriesScore = scoreTokens(seriesName, queryTokens, rawQuery);
        if (seriesScore > 0) continue; // series already visible, no need for book hits

        const auto& files = it.value();
        for (const auto& bf : files) {
            if (bookHitCount >= 24) break;
            int bookScore = scoreTokens(bf.title, queryTokens, rawQuery);
            if (bookScore > 0) {
                // Create a tile for this individual book
                QFileInfo fi(bf.filePath);
                QString fileKey = bf.filePath + "::" + QString::number(fi.size())
                                + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
                QString fileHash = QString(QCryptographicHash::hash(
                    fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QString fileCover = thumbsDir + "/" + fileHash + ".jpg";

                // Find series cover as fallback
                QString seriesHash = QString(QCryptographicHash::hash(
                    seriesPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
                QString seriesCover = thumbsDir + "/" + seriesHash + ".jpg";
                QString coverPath = QFile::exists(fileCover) ? fileCover :
                                    (QFile::exists(seriesCover) ? seriesCover : QString());

                auto* card = new TileCard(coverPath, bf.title, seriesName);
                card->setProperty("seriesPath", seriesPath);
                card->setProperty("filePath", bf.filePath);
                connect(card, &TileCard::clicked, this, [this, bf]() {
                    emit openBook(bf.filePath);
                });
                m_bookHitsStrip->addTile(card);
                bookHitCount++;
            }
        }
    }

    if (bookHitCount > 0)
        m_bookHitsSection->show();

    // Books section empty state
    if (m_bookStrip->visibleCount() == 0 && bookHitCount == 0) {
        m_bookStatus->setObjectName("LibraryEmptyLabel");
        m_bookStatus->setAlignment(Qt::AlignCenter);
        m_bookStatus->setText(
            QString("No results for \"%1\"").arg(rawQuery));
        m_bookStatus->show();
        m_bookStrip->hide();
    } else if (m_bookStrip->visibleCount() > 0) {
        m_bookStatus->hide();
        m_bookStrip->show();
    }
}

void BooksPage::refreshContinueStrip()
{
    m_continueStrip->clear();

    QJsonObject allProg = m_bridge->allProgress("books");
    if (allProg.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    struct ContinueItem {
        qint64 updatedAt;
        QString filePath;
        QString seriesPath;
        QString title;
        QString subtitle;
        QString coverPath;
    };
    QList<ContinueItem> items;

    for (auto it = allProg.begin(); it != allProg.end(); ++it) {
        QJsonObject prog = it.value().toObject();
        if (prog.value("finished").toBool())
            continue;
        int page = prog.value("page").toInt(0);
        if (page < 0)
            continue;

        auto ref = m_progressKeyMap.find(it.key());
        if (ref == m_progressKeyMap.end())
            continue;

        qint64 updatedAt = prog.value("updatedAt").toVariant().toLongLong();
        int pageCount = prog.value("pageCount").toInt(0);

        QString title = ScannerUtils::cleanMediaFolderTitle(QFileInfo(ref->filePath).completeBaseName());
        QString subtitle = pageCount > 0
            ? QString("Page %1/%2").arg(page + 1).arg(pageCount)
            : QString("Page %1").arg(page + 1);

        items.append({updatedAt, ref->filePath, ref->seriesPath, title, subtitle, ref->coverPath});
    }

    if (items.isEmpty()) {
        m_continueSection->hide();
        return;
    }

    // Per-series dedup: keep only the most recently updated book per series
    QMap<QString, int> bestPerSeries;
    for (int i = 0; i < items.size(); ++i) {
        auto it = bestPerSeries.find(items[i].seriesPath);
        if (it == bestPerSeries.end() || items[i].updatedAt > items[it.value()].updatedAt)
            bestPerSeries[items[i].seriesPath] = i;
    }

    QList<ContinueItem> deduped;
    for (int idx : bestPerSeries)
        deduped.append(items[idx]);

    std::sort(deduped.begin(), deduped.end(), [](const ContinueItem& a, const ContinueItem& b) {
        return a.updatedAt > b.updatedAt;
    });

    for (const auto& item : deduped) {
        auto* card = new TileCard(item.coverPath, item.title, item.subtitle);
        card->setProperty("filePath", item.filePath);
        card->setProperty("seriesPath", item.seriesPath);
        card->setProperty("seriesName", ScannerUtils::cleanMediaFolderTitle(QDir(item.seriesPath).dirName()));
        card->setProperty("coverPath", item.coverPath);
        // Store progress key for context menu operations
        QString progKey = QString(QCryptographicHash::hash(
            item.filePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        card->setProperty("progressKey", progKey);
        connect(card, &TileCard::clicked, this, [this, card]() {
            emit openBook(card->property("filePath").toString());
        });
        m_continueStrip->addTile(card);
    }

    m_continueSection->show();
}

void BooksPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- no-op restore. BooksPage
    // has no deep state today; the page is always on its landing view.
    Q_UNUSED(target);
}
