export const meta = {
  name: 'stream-episode-list-rewrite-investigate',
  description: 'Exhaustive investigate+diagnose+design pass for the StreamDetailView episode-list rewrite (virtualized model/view+delegate) and the click-isnt-downloading + app-unresponsive bugs',
  phases: [
    { title: 'Investigate', detail: 'parallel read-only investigators: dispatch/click-bug, render census, perf hotspots, target arch, API contract' },
    { title: 'Verify', detail: 'adversarial verification of the click-bug root cause' },
    { title: 'Design', detail: 'synthesize findings into a target architecture + TDD task breakdown' },
  ],
}

const REPO = 'c:/Users/Suprabha/Desktop/Tankoban 2'

// Flat-schema helpers — keep schemas shallow to avoid nested-brace mistakes.
const str = (description) => ({ type: 'string', description })
const strArr = (description) => ({ type: 'array', items: { type: 'string' }, description })
const obj = (required, properties) => ({ type: 'object', additionalProperties: false, required, properties })

const CTX = `
PROJECT: Tankoban 2 (C++ / Qt6 desktop app, Windows). You are a READ-ONLY investigator. Do NOT edit any files.
SYMPTOM (from the product owner): In Stream mode, opening a long show like One Piece (~1000+ episodes) is very slow,
scrolling the episode list is rough/janky, the whole app intermittently shows "Not Responding" and sometimes crashes, AND
clicking an episode does not start a download — the app is too frozen to tell what is happening.

KEY FILE: src/ui/pages/stream/StreamDetailView.cpp (+ .h) — the show-detail screen that renders the episode list.
ALREADY-ESTABLISHED FACTS (verify; do not just trust them):
- m_episodeTable is a QTableWidget. populateEpisodeTable() (~line 1043) builds, PER episode row, ~12 live QWidgets via
  setCellWidget across 4 columns (checkbox QToolButton, action QPushButton, thumbnail QLabel, stacked title+overview QLabels).
  setCellWidget defeats QTableView viewport virtualization (cell widgets are persistent, not painted-on-demand).
- A 1Hz QTimer (m_progressRefreshTimer, ~line 114) calls refreshAllEpisodeRows() (~line 1444) which loops EVERY row with no
  on-screen/visibility guard; each row -> refreshEpisodeRow (~1374) -> episodeDisplayState (~1281) which does QFileInfo::exists
  (a disk stat) per episode. The same full loop also fires on every StreamDownloadIndex entriesChanged/entryStateChanged.
- onActionIconClicked (~1798): NotDownloaded -> emit singleEpisodeDownloadRequested(season, episode); Downloaded -> play;
  Downloading -> pauseTorrent; Paused -> resumeTorrent; Failed -> re-dispatch. The ROW click onEpisodeActivated (~1237) does
  NOT download — it fires playRequested / source-pick.
- Repo conventions: grayscale UI only (no color, no emoji), SVG icons. Logs live under out/ (out/events.jsonl,
  out/sidecar_debug_live.log, out/stream_telemetry.log). git log is available for regression archaeology.
Return ONLY the structured object your schema asks for. Be concrete: cite file:line for every claim. Working dir: ${REPO}.
`

const DISPATCH_SCHEMA = obj(
  ['chain', 'downloadActuallyStarts', 'blockingCalls', 'rootCauseHypothesis', 'evidence', 'confidence'],
  {
    chain: strArr('ordered steps "file:line - what happens" from icon click to an actual download dispatch'),
    downloadActuallyStarts: str('yes / no / conditional - does a real download dispatch occur end to end, and under what condition?'),
    blockingCalls: strArr('any synchronous UI-thread call in the click->dispatch path (resolveMetadata, blocking QNetwork, nested QEventLoop, sleep, large disk walk), as "file:line - call - why it blocks"; empty if none'),
    regressionSuspect: str('recent commit (sha + subject) that may have broken the click->download path, or "none found"'),
    logEvidence: str('what out/events.jsonl / sidecar / stream_telemetry show (or do not show) for a download dispatch on recent activity'),
    rootCauseHypothesis: str('the single best explanation for "clicking an episode does not download"'),
    evidence: strArr('file:line facts supporting the hypothesis'),
    confidence: str('high / medium / low'),
  })

const RENDER_SCHEMA = obj(
  ['widgetsPerRow', 'dataModel', 'tableTouchpoints', 'couplings'],
  {
    widgetsPerRow: strArr('"column - widgets created - note" for each of the 4 cell-widget columns'),
    totalWidgetsFormula: str('formula for total live widgets for N episodes'),
    dataModel: str('the episode struct type + its fields + the container type (m_seasons maps season->episodes of what type?)'),
    tableTouchpoints: strArr('EVERY place that reads/writes m_episodeTable across the file, as "file:line - what" (rowCount loops, item(), cellWidget(), setItem, insertRow, setRowCount, selectRow, scrollToItem, currentRow, cellClicked/contextmenu connects, rowForEpisode, findInfoHashForEpisode) - the rewrite must port ALL of these'),
    couplings: strArr('external couplings driven off the table (m_selectedEpisodes set, calendar preselect via UserRole, season combo, context menu, status/progress cells)'),
  })

const PERF_SCHEMA = obj(
  ['hotspots', 'periodicWork', 'fullLoopTriggers'],
  {
    hotspots: strArr('ranked "file:line - cost - scalesWith - severity" worst first'),
    periodicWork: strArr('timers + interval + what each does per fire; NOTE whether the timer is stopped on hideEvent or keeps firing when the view is not visible'),
    fullLoopTriggers: strArr('every signal/event that triggers a full all-rows loop'),
    diskStatsPerSecondFormula: str('disk stats/sec and approx widget count for a 1000-episode One Piece season'),
    thumbnailFetchBehavior: str('how many QNetworkAccessManager requests can fire when opening a fresh show; throttled or not'),
    appWideSuspects: strArr('contributors to app-wide "Not Responding"/crash visible from the Stream domain: hidden-view timers still firing, leaked widgets/connections on re-show, unbounded growth, crash-prone null derefs'),
  })

const REF_SCHEMA = obj(
  ['recommendedArchitecture', 'inRepoExemplars', 'episodeTileState', 'interactionChallenges'],
  {
    recommendedArchitecture: str('concrete target for a 1000+ row grayscale list with per-row checkbox, async disk-cached thumbnail, stacked title+3-line overview, progress %, status glyph, per-row action control. Decide QAbstractListModel+QStyledItemDelegate on QListView vs QTableView+model+delegate; justify.'),
    inRepoExemplars: strArr('existing model/view+delegate usage in src/ to mirror, as "path - pattern - note" (search QAbstractItemModel, QAbstractListModel, QAbstractTableModel, QStyledItemDelegate, setItemDelegate, overridden data()/rowCount()/paint()/sizeHint())'),
    episodeTileState: str('current state of src/ui/pages/stream/EpisodeTile.{h,cpp} - exists? used? shape? it was the planned Phase D target'),
    comicsBooksApproach: str('how Comics (src/ui/pages/comics) and Books (src/ui/pages/tankolibrary) render long lists today - any reusable virtualized pattern, or QListWidget/manual layout?'),
    interactionChallenges: strArr('how per-row clickable controls work with a delegate (no real child widgets): delegate hit-testing on clicked()+option.rect, editor widget only under cursor, setIndexWidget for visible rows only, etc.'),
  })

const API_SCHEMA = obj(
  ['constructorCallers', 'publicMethods', 'signals', 'mustStayStable'],
  {
    constructorCallers: strArr('who constructs StreamDetailView and where (file:line)'),
    publicMethods: strArr('public methods from the .h as "signature - purpose - callers" (showEntry, setStreamDownloadIndex, setTorrentClient, currentSeason, etc.)'),
    signals: strArr('signals as "signature - connectedIn - what handler does" (download-request signals, playRequested, playLocalFileFromStreamRequested)'),
    mustStayStable: strArr('the external contract the rewrite MUST preserve byte-stable (signal sigs, public method sigs, construction) - rewrite is internal-only to episode-list rendering'),
  })

phase('Investigate')
const [dispatch, render, perf, ref, api] = await parallel([
  () => agent(CTX + `
YOUR TASK -- DISPATCH CHAIN + CLICK-ISNT-DOWNLOADING ROOT CAUSE (highest-priority correctness item).
1. Trace singleEpisodeDownloadRequested, seasonDownloadRequested, selectedEpisodesDownloadRequested from StreamDetailView to
   wherever they are connected (likely StreamPage.cpp or the owning page). Read the handler(s) fully.
2. Determine end to end whether clicking the download icon for a NotDownloaded episode actually results in a download
   dispatch (TorrentClient::startDownload / addMagnetHeadless / a stream-server dispatch). Where could it silently no-op?
   (signal not connected, handler early-returns, needs source resolution first that never completes, guarded by a flag.)
3. Find any UI-thread BLOCKING work in the click->dispatch path (synchronous metadata/source resolution, blocking QNAM,
   nested QEventLoop, QThread::sleep, big synchronous disk walk). This is the "freezes on click" suspect. cite file:line.
4. git log the click/download path (StreamDetailView.cpp, StreamPage.cpp, TorrentClient.cpp) over the last ~3 weeks; name any
   commit that plausibly regressed it.
5. Check out/events.jsonl (tail), out/sidecar_debug_live.log, out/stream_telemetry.log for whether a download/torrent-add
   event appears around recent activity. Report findings or that logs are silent.`,
    { label: 'dispatch+click-bug', phase: 'Investigate', schema: DISPATCH_SCHEMA }),

  () => agent(CTX + `
YOUR TASK -- RENDER ARCHITECTURE CENSUS (everything the rewrite must replace).
Read populateEpisodeTable fully + the m_episodeTable setup in buildUI. Produce: exact widgets per row per column (+ total
formula for N episodes); the episode data model (struct type, fields, container m_seasons); EVERY m_episodeTable touchpoint
across the whole file as file:line (rowCount loops, item(), cellWidget(), setItem, insertRow, setRowCount, selectRow,
scrollToItem, currentRow, cellClicked/customContextMenu connects, rowForEpisode, findInfoHashForEpisode); and external
couplings (m_selectedEpisodes, calendar preselect via UserRole, season combo, context menu, status/progress cells).`,
    { label: 'render-census', phase: 'Investigate', schema: RENDER_SCHEMA }),

  () => agent(CTX + `
YOUR TASK -- PERFORMANCE HOTSPOT MAP (rank by impact for a 1000-episode season) + APP-WIDE SUSPECTS.
1. The 1Hz m_progressRefreshTimer: confirm interval, what it calls per fire, total per-fire cost for N rows, and CRUCIALLY
   whether it is stopped in hideEvent() / started in showEvent() or whether it keeps firing while the view is hidden.
2. refreshAllEpisodeRows / refreshEpisodeRow / episodeDisplayState: count disk stats (QFileInfo::exists), index lookups
   (bestEntryForEpisode), torrent snapshot calls (streamBulkSnapshotForImdbSeason) per row per fire.
3. Enumerate EVERY trigger of a full all-rows loop (timer, entriesChanged, entryStateChanged, populate).
4. Thumbnail fetch in populate: how many QNAM requests can fire opening a fresh show; throttled? (fetchEpisodeThumbnail/NetSeam).
5. Formula: disk stats/sec and widget count for a 1000-episode One Piece season.
6. APP-WIDE: from the Stream domain, list anything that could cause app-wide "Not Responding" or crashes -- hidden-view timers
   still firing, leaked QObject connections/widgets across re-show, unbounded container growth, crash-prone null derefs in the
   refresh path. Rank everything; say what to kill first.`,
    { label: 'perf-hotspots', phase: 'Investigate', schema: PERF_SCHEMA }),

  () => agent(CTX + `
YOUR TASK -- TARGET ARCHITECTURE + IN-REPO REFERENCE PATTERN.
1. Search src/ for correct Qt model/view usage to MIRROR (QAbstractItemModel, QAbstractListModel, QAbstractTableModel,
   QStyledItemDelegate, QItemDelegate, setItemDelegate, overridden data()/rowCount()/paint()/sizeHint()). List exemplars.
2. Recommend a concrete target for a 1000+ row grayscale episode list with per-row checkbox (multi-select), async disk-cached
   thumbnail, stacked title + 3-line overview, progress %, status glyph, per-row action control (download/pause/resume/play).
   Decide custom QAbstractListModel + QStyledItemDelegate on a QListView (virtualized painted rows) vs QTableView+model+
   delegate; justify. Address how clickable per-row controls work WITHOUT real child widgets (delegate hit-testing on
   QAbstractItemView::clicked + visualRect/option.rect math; editor widget only under cursor; or setIndexWidget for visible
   rows only). Enumerate interactionChallenges.
3. Report current state of src/ui/pages/stream/EpisodeTile.{h,cpp} (planned Phase D widget).
4. How do Comics (src/ui/pages/comics) and Books (src/ui/pages/tankolibrary) render long lists today -- reusable pattern?`,
    { label: 'target-arch+reference', phase: 'Investigate', schema: REF_SCHEMA }),

  () => agent(CTX + `
YOUR TASK -- EXTERNAL API / SIGNAL CONTRACT (so the rewrite preserves behavior).
1. Who constructs StreamDetailView and where (grep the class name across src/).
2. Public methods from the .h with purpose + callers (showEntry, setStreamDownloadIndex, setTorrentClient, currentSeason...).
3. Signals, where each is connected, and what the handler does (download-request signals, playRequested,
   playLocalFileFromStreamRequested).
4. State the external contract that MUST stay byte-stable through the rewrite. The rewrite is internal-only to the
   episode-list rendering.`,
    { label: 'api-contract', phase: 'Investigate', schema: API_SCHEMA }),
])

phase('Verify')
const VERIFY_SCHEMA = obj(
  ['verdict', 'reasoning'],
  {
    verdict: str('confirmed / refuted / uncertain'),
    reasoning: str('why, with file:line'),
    counterEvidence: str('anything that contradicts the hypothesis'),
    correction: str('if refuted/uncertain, the corrected explanation'),
  })

const dHyp = dispatch ? dispatch.rootCauseHypothesis : '(none)'
const dChain = dispatch ? JSON.stringify(dispatch.chain) : '[]'
const dBlock = dispatch ? JSON.stringify(dispatch.blockingCalls) : '[]'
const dStarts = dispatch ? dispatch.downloadActuallyStarts : '?'

const clickVerdicts = await parallel([
  () => agent(CTX + `
ADVERSARIAL VERIFICATION. A prior investigator concluded this about "clicking an episode does not download":
HYPOTHESIS: ${dHyp}
CHAIN: ${dChain}
BLOCKING CALLS: ${dBlock}
DOWNLOAD ACTUALLY STARTS: ${dStarts}
Try to REFUTE it. Read the actual code yourself (onActionIconClicked + the handler of singleEpisodeDownloadRequested). Is it
correct, or is the real reason different (user clicking the ROW which only opens sources; the download DOES start but the
frozen UI hides it; a source must be resolved first)? Default to "uncertain" if evidence is thin.`,
    { label: 'verify:click-bug-A', phase: 'Verify', schema: VERIFY_SCHEMA }),
  () => agent(CTX + `
ADVERSARIAL VERIFICATION (independent second opinion). Same hypothesis: "${dHyp}". Investigate from a DIFFERENT angle: is it
a UI-thread freeze MASKING a working dispatch, or a genuinely broken dispatch? Distinguish by reading the dispatch handler and
by reasoning about whether the freeze (1Hz disk-stat loop / ~12k widgets) would even let the click handler run. cite file:line.`,
    { label: 'verify:click-bug-B', phase: 'Verify', schema: VERIFY_SCHEMA }),
])

phase('Design')
const DESIGN_SCHEMA = obj(
  ['architecture', 'components', 'tasks', 'clickBugFix', 'externalContractPreserved', 'risks'],
  {
    architecture: str('the chosen target architecture in one tight paragraph'),
    components: strArr('new/changed components as "name | file | responsibility"'),
    tasks: strArr('numbered, independently-buildable, TDD-friendly tasks as "T# | title | scope | files | risk | test/verify". Each must BUILD OK on its own and preserve the external contract. Note which existing m_episodeTable touchpoints each ports.'),
    clickBugFix: str('how the click-isnt-downloading bug is addressed (separately from perf if it is a real bug)'),
    externalContractPreserved: strArr('signal/method/construction contract kept byte-stable'),
    risks: strArr('risks + mitigations'),
    openQuestions: strArr('anything needing the owner decision before/while executing'),
  })

const design = await agent(`
You are the lead architect synthesizing an implementation design to rewrite the Tankoban 2 Stream-mode episode list
(src/ui/pages/stream/StreamDetailView.cpp) from a QTableWidget-with-cell-widgets to a VIRTUALIZED Qt model/view + delegate,
AND to fix a "clicking an episode does not download" bug and the app-wide unresponsiveness it contributes to. Below are five
investigator reports + two adversarial verdicts on the click bug. Produce a concrete, TDD-friendly, incrementally-shippable
design with a numbered task breakdown. Each task must be independently buildable (BUILD OK gate) and preserve the external
signal/method contract (rewrite is internal-only to episode-list rendering). Prefer a pure QAbstractListModel +
QStyledItemDelegate on a QListView, with per-row controls handled via delegate hit-testing on clicked()+option.rect, and
async disk-cached thumbnails via the model decorationRole with a visible-range fetch. Call out exactly which existing
m_episodeTable touchpoints each task ports. Keep grayscale-only, SVG-icon, no-emoji rules. Sequence tasks so the perf relief
(kill the 1Hz all-rows disk-stat loop + the widget explosion) lands earliest, and the click-bug fix is explicit.

=== DISPATCH / CLICK-BUG REPORT ===
${JSON.stringify(dispatch, null, 2)}
=== ADVERSARIAL VERDICTS ON CLICK BUG ===
${JSON.stringify(clickVerdicts, null, 2)}
=== RENDER CENSUS ===
${JSON.stringify(render, null, 2)}
=== PERF HOTSPOTS ===
${JSON.stringify(perf, null, 2)}
=== TARGET ARCH / REFERENCE ===
${JSON.stringify(ref, null, 2)}
=== API / SIGNAL CONTRACT ===
${JSON.stringify(api, null, 2)}
`, { label: 'synthesize-design', phase: 'Design', schema: DESIGN_SCHEMA })

return { dispatch, render, perf, ref, api, clickVerdicts, design }
