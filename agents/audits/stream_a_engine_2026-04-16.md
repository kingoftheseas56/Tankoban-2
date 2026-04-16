# Tankoban 2 Stream-A Engine Substrate Comparative Audit - 2026-04-16

Author: Agent 7 (Codex)  
Mode: Trigger C comparative audit, reference only  
Slice: A of 6, first in sequence A -> D -> 3a -> C -> 3b -> 3c  
Domain master: Agent 4 (Stream mode)

This report is advisory. It compares Tankoban's current stream server / engine substrate with perpetus `stream-server-master` and selected Stremio consumer-side contracts. It does not modify `src/`, does not prescribe a single implementation, and separates cited observations from hypotheses for Agent 4 to validate.

## Scope

In scope: Tankoban byte-serving substrate (`StreamEngine`, `StreamHttpServer`, `TorrentEngine`), head-piece startup, piece priority, `contiguousBytesFromOffset`, HTTP 206 serving, cache/eviction, cancellation propagation, HLS/transcoding substrate, trackers/DHT, backend abstraction, stats/health, and the server/add-on protocol line.

Out of scope: player UX details, source picker behavior, library progress, and add-on ordering except as cross-slice appendix items.

Closed STREAM_LIFECYCLE_FIX context:

- Treated as closed and not re-opened unless a distinct substrate gap appears: P0-1 source-switch flash-to-browse, P1-1 stale `m_infoHash`, P1-2 3s failure timer yank-back, P1-4 `StreamEngine::streamError` wiring/no-video failure, P1-5 VideoPlayer re-open race, P2-2 prefetch hygiene, and P2-3 Shift+N silent no-op.
- Current Tankoban sets the per-stream cancellation token before erasing the stream record and before torrent removal (`src/core/stream/StreamEngine.cpp:294`, `src/core/stream/StreamEngine.cpp:304`, `src/core/stream/StreamEngine.cpp:322`). The HTTP wait path checks the token before touching `TorrentEngine` (`src/core/stream/StreamHttpServer.cpp:89`, `src/core/stream/StreamHttpServer.cpp:99`, `src/core/stream/StreamHttpServer.cpp:102`).
- Every finding below states overlap or non-overlap with this closed work.

## Methodology

Tankoban files read: `src/core/stream/StreamEngine.h`, `src/core/stream/StreamEngine.cpp`, `src/core/stream/StreamHttpServer.h`, `src/core/stream/StreamHttpServer.cpp`, `src/core/torrent/TorrentEngine.h`, `src/core/torrent/TorrentEngine.cpp`, `src/core/torrent/TorrentClient.h`, `src/core/torrent/TorrentClient.cpp`, `src/core/stream/addon/StreamInfo.h`, and `src/core/stream/addon/StreamSource.h`.

Repo context read: `AGENTS.md`, `agents/GOVERNANCE.md`, `agents/VERSIONS.md`, `agents/CONTRACTS.md`, `agents/STATUS.md`, `agents/REVIEW.md`, targeted `agents/chat.md` posts including the stream audit ratification at `agents/chat.md:3909`, `STREAM_LIFECYCLE_FIX_TODO.md`, `agents/audits/README.md`, `agents/audits/edge_tts_2026-04-16.md`, and `agents/audits/video_player_comprehensive_2026-04-16.md`.

Primary perpetus files read: `enginefs/src/backend/`, `enginefs/src/backend/libtorrent/`, `enginefs/src/cache.rs`, `enginefs/src/disk_cache.rs`, `enginefs/src/engine.rs`, `enginefs/src/hls.rs`, `enginefs/src/hwaccel.rs`, `enginefs/src/lib.rs`, `enginefs/src/piece_cache.rs`, `enginefs/src/piece_waiter.rs`, `enginefs/src/subtitles.rs`, `enginefs/src/tracker_prober.rs`, `enginefs/src/trackers.rs`, `server/src/routes/`, `server/src/cache_cleaner.rs`, `server/src/ffmpeg_setup.rs`, `server/src/local_addon/mod.rs`, `server/src/main.rs`, `server/src/state.rs`, and bindings under `bindings/async-rar`, `bindings/async-sevenz`, and `bindings/libtorrent-sys`.

Secondary Stremio references read: `stremio-video-master/src/withStreamingServer/*.js`, `stremio-core-development/src/types/streaming_server/*.rs`, `stremio-core-development/src/models/streaming_server.rs`, `stremio-service-master/src/server.rs`, and `stremio-docker-main/Dockerfile`. The downloaded archives contain nested top-level folders, so the actual read paths include those nested folder names.

Web sources cited: libtorrent 2.0.11 `torrent_handle` reference at https://libtorrent.org/reference-Torrent_Handle.html, RFC 9110 HTTP Semantics at https://www.ietf.org/ietf-ftp/rfc/rfc9110.pdf, and public webtor Stremio service docs at https://pkg.go.dev/github.com/webtor-io/web-ui/services/stremio.

Torrentio note: I did not find a clean public real Torrentio `/stream/series/<id>.json` response with enough provenance to cite. Torrentio-specific add-on response assumptions are therefore marked as validation gaps, not observed facts.

## Findings Per Comparison Axis

### 1. Head-Piece Prefetch Heuristic And `contiguousBytesFromOffset`

Observations:

- Tankoban gates readiness on a contiguous 5 MB head window. Progress is `contiguousHead / 5 MB` (`src/core/stream/StreamEngine.cpp:200`, `src/core/stream/StreamEngine.cpp:220`, `src/core/stream/StreamEngine.cpp:227`) and `streamFile` returns `FILE_NOT_READY` until `contiguousHead >= gateSize` (`src/core/stream/StreamEngine.cpp:267`, `src/core/stream/StreamEngine.cpp:270`).
- Tankoban sets head deadlines over the first 5 MB with a 500 ms to 5000 ms gradient (`src/core/stream/StreamEngine.cpp:578`, `src/core/stream/StreamEngine.cpp:589`, `src/core/stream/StreamEngine.cpp:608`, `src/core/stream/StreamEngine.cpp:615`). It also sets the selected file to priority 7 and other files to 0 (`src/core/stream/StreamEngine.cpp:715`, `src/core/stream/StreamEngine.cpp:717`, `src/core/stream/StreamEngine.cpp:719`).
- Tankoban enables sequential download after adding a magnet (`src/core/stream/StreamEngine.cpp:155`, `src/core/stream/StreamEngine.cpp:156`) while its comments acknowledge deadlines as the main streaming primitive (`src/core/stream/StreamEngine.cpp:569`, `src/core/stream/StreamEngine.cpp:583`).
- Tankoban `contiguousBytesFromOffset` maps file offset to a starting piece, walks whole pieces until the first missing piece, and estimates byte contribution within file bounds (`src/core/torrent/TorrentEngine.cpp:1141`, `src/core/torrent/TorrentEngine.cpp:1162`, `src/core/torrent/TorrentEngine.cpp:1168`, `src/core/torrent/TorrentEngine.cpp:1195`). libtorrent documents `have_piece` as true only after the piece is fully downloaded and written (https://libtorrent.org/reference-Torrent_Handle.html, fetched page lines 336-340).
- Perpetus uses a smaller initial target: `MIN_STARTUP_BYTES = 1 MB`, max two startup pieces (`stream-server-master/enginefs/src/backend/priorities.rs:3`, `stream-server-master/enginefs/src/backend/priorities.rs:6`, `stream-server-master/enginefs/src/backend/priorities.rs:8`). Offset 0 is `InitialPlayback`, with a 0 ms base deadline over that small startup window (`stream-server-master/enginefs/src/backend/libtorrent/handle.rs:218`, `stream-server-master/enginefs/src/backend/libtorrent/handle.rs:262`, `stream-server-master/enginefs/src/backend/libtorrent/handle.rs:270`, `stream-server-master/enginefs/src/backend/libtorrent/handle.rs:275`).
- Perpetus expands priorities dynamically while reading. It computes urgent windows from bitrate/download speed/cache state (`stream-server-master/enginefs/src/backend/priorities.rs:72`, `stream-server-master/enginefs/src/backend/priorities.rs:78`, `stream-server-master/enginefs/src/backend/priorities.rs:129`) and calls `set_priorities` during `poll_read` (`stream-server-master/enginefs/src/backend/libtorrent/stream.rs:183`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:184`).
- Perpetus uses exact piece waiters instead of Tankoban's polling loop: readers register a waker for the missing piece (`stream-server-master/enginefs/src/piece_waiter.rs:25`, `stream-server-master/enginefs/src/piece_waiter.rs:27`), and the alert pump wakes waiters after caching piece bytes (`stream-server-master/enginefs/src/backend/libtorrent/mod.rs:227`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:245`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:246`).
- Perpetus handles the head/tail metadata gap by deferring tail metadata deadlines until startup pieces are in flight (`stream-server-master/enginefs/src/backend/libtorrent/handle.rs:322`, `stream-server-master/enginefs/src/backend/libtorrent/handle.rs:329`) and preserving head priorities during container-metadata seeks (`stream-server-master/enginefs/src/backend/libtorrent/stream.rs:92`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:99`).
- STREAM_LIFECYCLE_FIX overlap: non-overlap. This is startup/piece scheduling, not source-switch cancellation.

Hypotheses:

- Hypothesis — Tankoban's 5 MB readiness gate may be materially more conservative than perpetus's first-playable-piece policy, and may amplify startup stalls when the swarm has peers but early pieces are not contiguous. Agent 4 to validate.
- Hypothesis — `contiguousBytesFromOffset` may understate playable readiness for containers whose first probe needs less than 5 MB, while also not modeling tail metadata needs for MP4/MKV. Agent 4 to validate.
- Hypothesis — Agent 4 needs to decide whether Tankoban's sidecar-specific 5 MB probe constraint remains the governing substrate target, or whether the substrate should move toward a smaller initial gate plus dynamic reader-driven prioritization. Agent 4 to validate.

### 2. Byte-Range Serving (HTTP 206) Shape And Behavior

Observations:

- Tankoban parses a single byte range. Suffix and open-ended ranges are supported, but multi-range headers are not (`src/core/stream/StreamHttpServer.cpp:43`, `src/core/stream/StreamHttpServer.cpp:56`, `src/core/stream/StreamHttpServer.cpp:67`, `src/core/stream/StreamHttpServer.cpp:70`).
- Tankoban returns 416 with `Content-Range: bytes */<size>`, `Content-Length: 0`, and close (`src/core/stream/StreamHttpServer.cpp:223`, `src/core/stream/StreamHttpServer.cpp:226`, `src/core/stream/StreamHttpServer.cpp:227`, `src/core/stream/StreamHttpServer.cpp:229`). RFC 9110 says 416 byte-range responses should include a `Content-Range` current length (https://www.ietf.org/ietf-ftp/rfc/rfc9110.pdf, fetched PDF lines 5595-5602).
- Tankoban 206 responses include `Content-Range`, `Content-Length`, `Accept-Ranges: bytes`, `Cache-Control: no-store`, and `Connection: close` (`src/core/stream/StreamHttpServer.cpp:241`, `src/core/stream/StreamHttpServer.cpp:250`, `src/core/stream/StreamHttpServer.cpp:251`, `src/core/stream/StreamHttpServer.cpp:252`, `src/core/stream/StreamHttpServer.cpp:253`). RFC 9110 describes 206 `Content-Range` and `Accept-Ranges: bytes` semantics (fetched PDF lines 4704-4708 and 4719-4729).
- Tankoban serves 256 KB chunks and blocks each chunk until `haveContiguousBytes` reports the requested chunk available (`src/core/stream/StreamHttpServer.cpp:76`, `src/core/stream/StreamHttpServer.cpp:307`, `src/core/stream/StreamHttpServer.cpp:327`). On timeout or cancellation it closes the socket for decoder retry (`src/core/stream/StreamHttpServer.cpp:336`, `src/core/stream/StreamHttpServer.cpp:341`, `src/core/stream/StreamHttpServer.cpp:343`).
- Perpetus exposes both `/stream/{infoHash}/{fileIdx}` and `/{infoHash}/{fileIdx}` routes (`stream-server-master/server/src/main.rs:229`, `stream-server-master/server/src/main.rs:231`, `stream-server-master/server/src/main.rs:235`). Tankoban currently builds `/stream/{hash}/{fileIndex}` local URLs (`src/core/stream/StreamEngine.h:25`).
- Perpetus' parser is also single-range only (`stream-server-master/server/src/routes/stream.rs:368`, `stream-server-master/server/src/routes/stream.rs:374`, `stream-server-master/server/src/routes/stream.rs:375`, `stream-server-master/server/src/routes/stream.rs:376`). It sets range headers and uses `take(content_length)` before returning an Axum body stream (`stream-server-master/server/src/routes/stream.rs:320`, `stream-server-master/server/src/routes/stream.rs:321`, `stream-server-master/server/src/routes/stream.rs:322`, `stream-server-master/server/src/routes/stream.rs:333`, `stream-server-master/server/src/routes/stream.rs:338`).
- STREAM_LIFECYCLE_FIX overlap: partial overlap only for cancellation on in-flight piece waits; range/header/route shape is non-overlap.

Hypotheses:

- Hypothesis — Single-range support is not a perpetus divergence, but the missing bare `/{infoHash}/{fileIdx}` route may matter if downstream code reuses Stremio consumer assumptions. Agent 4 to validate.
- Hypothesis — Tankoban's socket-close-on-piece-timeout contract may be valid for the native sidecar, but it differs from perpetus' pending async body stream and should be validated under repeated mid-file starvation. Agent 4 to validate.

### 3. Buffer / Cache Eviction Policy

Observations:

- Tankoban streams from libtorrent's disk-backed save path and deletes data on `stopStream` through `removeTorrent(..., true)` (`src/core/stream/StreamEngine.cpp:322`, `src/core/stream/StreamEngine.cpp:324`). Its HTTP response `Cache-Control: no-store` is client-cache policy, not a reusable torrent cache (`src/core/stream/StreamHttpServer.cpp:253`).
- Perpetus has a memory-first piece cache with optional disk persistence. Memory is 5% of disk cache size up to 512 MB when disk cache is enabled (`stream-server-master/enginefs/src/piece_cache.rs:37`, `stream-server-master/enginefs/src/piece_cache.rs:40`, `stream-server-master/enginefs/src/piece_cache.rs:45`) and uses a 5-minute time-to-idle policy (`stream-server-master/enginefs/src/piece_cache.rs:73`, `stream-server-master/enginefs/src/piece_cache.rs:82`, `stream-server-master/enginefs/src/piece_cache.rs:88`).
- Perpetus serves from local cached piece data first, then Moka piece cache, then waits for libtorrent (`stream-server-master/enginefs/src/backend/libtorrent/stream.rs:193`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:201`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:234`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:317`). It adaptively prefetches already downloaded next pieces into memory (`stream-server-master/enginefs/src/backend/libtorrent/stream.rs:265`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:273`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:284`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:301`).
- Perpetus conditionally persists completed files that fit cache limits (`stream-server-master/enginefs/src/disk_cache.rs:1`, `stream-server-master/enginefs/src/disk_cache.rs:41`, `stream-server-master/enginefs/src/disk_cache.rs:55`, `stream-server-master/enginefs/src/disk_cache.rs:61`, `stream-server-master/enginefs/src/disk_cache.rs:70`) and has a cache cleaner that protects active files, deletes old files, and evicts oldest files on size pressure (`stream-server-master/server/src/cache_cleaner.rs:105`, `stream-server-master/server/src/cache_cleaner.rs:121`, `stream-server-master/server/src/cache_cleaner.rs:188`, `stream-server-master/server/src/cache_cleaner.rs:195`).
- Perpetus removes inactive engines after a 5-minute idle window only when active streams are zero (`stream-server-master/enginefs/src/lib.rs:123`, `stream-server-master/enginefs/src/lib.rs:127`, `stream-server-master/enginefs/src/lib.rs:140`, `stream-server-master/enginefs/src/lib.rs:155`).
- STREAM_LIFECYCLE_FIX overlap: P2-2 prefetch hygiene overlaps only in stale-deadline cleanup; cache retention/reseek behavior is new/non-overlap.

Hypotheses:

- Hypothesis — Tankoban's delete-on-stop model may make quick re-open/re-seek behavior worse than a reference-style memory/disk cache, especially after player-side re-open races are fixed. Agent 4 to validate.
- Hypothesis — Cache strategy needs a Slice-A boundary decision before Slice D and 3a audits interpret re-open latency as player or library-progress defects. Agent 4 to validate.

### 4. Cancellation Propagation

Observations:

- Tankoban's lifecycle hardening is present in current source: `stopStream` stores `true` on the token before erasing the stream and before deleting torrent data (`src/core/stream/StreamEngine.cpp:294`, `src/core/stream/StreamEngine.cpp:304`, `src/core/stream/StreamEngine.cpp:309`, `src/core/stream/StreamEngine.cpp:324`). Token lifetime is documented in `StreamEngine.h` (`src/core/stream/StreamEngine.h:111`, `src/core/stream/StreamEngine.h:154`, `src/core/stream/StreamEngine.h:159`).
- Tankoban's HTTP wait loop checks cancellation before `haveContiguousBytes` (`src/core/stream/StreamHttpServer.cpp:89`, `src/core/stream/StreamHttpServer.cpp:99`, `src/core/stream/StreamHttpServer.cpp:102`) and logs cancellation separately from timeout (`src/core/stream/StreamHttpServer.cpp:330`, `src/core/stream/StreamHttpServer.cpp:334`, `src/core/stream/StreamHttpServer.cpp:337`).
- Tankoban server shutdown has a cooperative shutdown flag and 2s drain window (`src/core/stream/StreamHttpServer.cpp:405`, `src/core/stream/StreamHttpServer.cpp:416`, `src/core/stream/StreamHttpServer.cpp:420`, `src/core/stream/StreamHttpServer.cpp:441`).
- Perpetus uses Drop/RAII rather than an explicit atomic token. `StreamGuard` calls `on_stream_end` when the body stream drops (`stream-server-master/server/src/routes/stream.rs:18`, `stream-server-master/server/src/routes/stream.rs:39`, `stream-server-master/server/src/routes/stream.rs:48`, `stream-server-master/server/src/routes/stream.rs:49`). It then delays file cleanup by 5s and clears file priorities only if no replacement stream is active (`stream-server-master/enginefs/src/lib.rs:295`, `stream-server-master/enginefs/src/lib.rs:311`, `stream-server-master/enginefs/src/lib.rs:330`, `stream-server-master/enginefs/src/lib.rs:366`).
- Perpetus transcode processes also terminate on Drop (`stream-server-master/enginefs/src/hls.rs:83`, `stream-server-master/enginefs/src/hls.rs:87`, `stream-server-master/enginefs/src/hls.rs:90`).
- STREAM_LIFECYCLE_FIX overlap: strong overlap. Tankoban's set-before-erase ordering addresses the same class of in-flight-stream cleanup that perpetus handles through Drop and delayed cleanup. This audit does not identify a new P0 cancellation defect from code-read alone.

Hypotheses:

- Hypothesis — Tankoban's token design is adequate for the closed lifecycle class if close-while-buffering logs keep showing `piece wait cancelled` rather than stale-handle crashes, hangs, or post-removal reads. Agent 4 to validate.
- Hypothesis — Any remaining divergence after cancellation likely belongs to cache retention and post-drop priority cleanup, not the closed cancellation-token ordering itself. Agent 4 to validate.

### 5. Subtitle Extraction Flow

Observations:

- Tankoban carries add-on subtitle and behavior-hint data (`src/core/stream/addon/StreamInfo.h:30`, `src/core/stream/addon/StreamInfo.h:35`, `src/core/stream/addon/StreamInfo.h:36`) but StreamEngine exposes no server-side VTT route in the files read. It converts magnet/direct/YouTube sources and rejects YouTube (`src/core/stream/StreamEngine.cpp:71`, `src/core/stream/StreamEngine.cpp:86`, `src/core/stream/StreamEngine.cpp:100`).
- Perpetus exposes `/subtitlesTracks`, `/{infoHash}/{fileIdx}/subtitles.vtt`, and `/subtitles.vtt` proxy routes (`stream-server-master/server/src/main.rs:238`, `stream-server-master/server/src/main.rs:242`, `stream-server-master/server/src/main.rs:251`). `subtitles_tracks` extracts an info hash from the URL and returns server-local VTT URLs (`stream-server-master/server/src/routes/subtitles.rs:74`, `stream-server-master/server/src/routes/subtitles.rs:81`, `stream-server-master/server/src/routes/subtitles.rs:90`, `stream-server-master/server/src/routes/subtitles.rs:99`).
- Perpetus detects external subtitle files inside the torrent (`stream-server-master/enginefs/src/engine.rs:334`, `stream-server-master/enginefs/src/engine.rs:341`, `stream-server-master/enginefs/src/engine.rs:345`) and probes text embedded subtitles on the main video (`stream-server-master/enginefs/src/engine.rs:355`, `stream-server-master/enginefs/src/engine.rs:378`, `stream-server-master/enginefs/src/engine.rs:386`). Extraction maps a subtitle stream to WebVTT via ffmpeg (`stream-server-master/enginefs/src/engine.rs:431`, `stream-server-master/enginefs/src/engine.rs:452`, `stream-server-master/enginefs/src/engine.rs:466`, `stream-server-master/enginefs/src/engine.rs:472`).
- Perpetus explicitly excludes bitmap subtitle OCR from this path; it accepts text codecs such as ASS, SSA, SubRip, WebVTT, mov_text, and text (`stream-server-master/enginefs/src/engine.rs:527`, `stream-server-master/enginefs/src/engine.rs:528`, `stream-server-master/enginefs/src/engine.rs:536`).
- Stremio video rewrites external subtitle URLs through the server when transcoding or adding extra subtitle tracks (`stremio-video-master/src/withStreamingServer/withStreamingServer.js:173`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:177`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:253`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:264`).
- STREAM_LIFECYCLE_FIX overlap: non-overlap.

Hypotheses:

- Hypothesis — Tankoban may intentionally keep subtitle extraction in the sidecar/player slice; if so, Stream-A should document that it is not trying to emulate Stremio's server-as-subtitle-source pattern. Agent 4 to validate.
- Hypothesis — If later Slice D expects Stremio-like subtitle proxy behavior, missing Stream-A subtitle endpoints could be misdiagnosed as player-only behavior unless logged as a substrate boundary. Agent 4 to validate.

### 6. HLS / Transcoding Triggers

Observations:

- Tankoban Stream-A returns either local HTTP for torrent-backed streams or direct URL for HTTP/URL streams (`src/core/stream/StreamEngine.h:17`, `src/core/stream/StreamEngine.h:25`, `src/core/stream/StreamEngine.cpp:86`, `src/core/stream/StreamEngine.cpp:95`). The Stream-A files read expose no HLS route, ffmpeg setup, probe endpoint, or transcoding decision.
- Stremio video probes direct-play support and rewrites unsupported or forced streams to `/hlsv2/<id>/master.m3u8?mediaURL=...` with HLS content-type hints (`stremio-video-master/src/withStreamingServer/withStreamingServer.js:134`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:151`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:172`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:184`). It probes `/hlsv2/probe?mediaURL=...` when transcoding support exists (`stremio-video-master/src/withStreamingServer/withStreamingServer.js:351`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:359`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:360`).
- Perpetus exposes HLS probe and playlist routes (`stream-server-master/server/src/main.rs:266`, `stream-server-master/server/src/main.rs:268`, `stream-server-master/server/src/main.rs:288`, `stream-server-master/server/src/main.rs:291`). The probe route parses `mediaURL` and returns Stremio-shaped probe JSON (`stream-server-master/server/src/routes/hls.rs:56`, `stream-server-master/server/src/routes/hls.rs:63`, `stream-server-master/server/src/routes/hls.rs:91`, `stream-server-master/server/src/routes/hls.rs:105`).
- Perpetus HLS uses 4s segments, H264/AAC targets, audio and subtitle media entries, and VOD playlists (`stream-server-master/enginefs/src/hls.rs:16`, `stream-server-master/enginefs/src/hls.rs:27`, `stream-server-master/enginefs/src/hls.rs:295`, `stream-server-master/enginefs/src/hls.rs:331`, `stream-server-master/enginefs/src/hls.rs:404`, `stream-server-master/enginefs/src/hls.rs:423`).
- Perpetus segment routes choose a local file path when available and otherwise use the stream URL, then launch ffmpeg segment transcodes (`stream-server-master/server/src/routes/hls.rs:425`, `stream-server-master/server/src/routes/hls.rs:426`, `stream-server-master/server/src/routes/hls.rs:451`, `stream-server-master/server/src/routes/hls.rs:485`, `stream-server-master/server/src/routes/hls.rs:466`).
- Perpetus selects hardware encoders from profile/auto-detect and falls back to software (`stream-server-master/enginefs/src/hwaccel.rs:45`, `stream-server-master/enginefs/src/hwaccel.rs:53`, `stream-server-master/enginefs/src/hwaccel.rs:168`, `stream-server-master/enginefs/src/hwaccel.rs:186`, `stream-server-master/enginefs/src/hwaccel.rs:257`). The server can auto-download Windows FFmpeg if it is missing (`stream-server-master/server/src/ffmpeg_setup.rs:11`, `stream-server-master/server/src/ffmpeg_setup.rs:18`, `stream-server-master/server/src/ffmpeg_setup.rs:23`, `stream-server-master/server/src/ffmpeg_setup.rs:72`, `stream-server-master/server/src/ffmpeg_setup.rs:144`).
- Stremio Docker fetches proprietary `server.js` at build time and bundles FFmpeg-oriented dependencies (`stremio-docker-main/Dockerfile:77`, `stremio-docker-main/Dockerfile:91`, `stremio-docker-main/Dockerfile:93`).
- STREAM_LIFECYCLE_FIX overlap: non-overlap.

Hypotheses:

- Hypothesis — HLS/transcoding is a major reference-surface gap rather than a small bug, and Agent 4 may need to classify it as a compatibility tier before player-side audits rely on it. Agent 4 to validate.
- Hypothesis — For Tankoban's native sidecar, unsupported-codec handling may belong outside Stream-A, but the lack of a Stremio-like probe decision point remains a substrate divergence. Agent 4 to validate.

### 7. Tracker / DHT Behavior

Observations:

- Tankoban enables DHT, LSD, NAT-PMP, UPnP, and fixed DHT bootstrap nodes (`src/core/torrent/TorrentEngine.cpp:190`, `src/core/torrent/TorrentEngine.cpp:196`, `src/core/torrent/TorrentEngine.cpp:197`, `src/core/torrent/TorrentEngine.cpp:198`, `src/core/torrent/TorrentEngine.cpp:199`). It announces to all trackers and tiers (`src/core/torrent/TorrentEngine.cpp:244`, `src/core/torrent/TorrentEngine.cpp:245`, `src/core/torrent/TorrentEngine.cpp:246`).
- Tankoban includes add-on trackers in generated magnet URIs (`src/core/stream/addon/StreamSource.h:71`, `src/core/stream/addon/StreamSource.h:72`, `src/core/stream/addon/StreamSource.h:73`).
- Stremio video sends `peerSearch.sources` including `dht:<infoHash>` plus trackers and min/max 40/200 (`stremio-video-master/src/withStreamingServer/createTorrent.js:29`, `stremio-video-master/src/withStreamingServer/createTorrent.js:31`, `stremio-video-master/src/withStreamingServer/createTorrent.js:36`, `stremio-video-master/src/withStreamingServer/createTorrent.js:37`). Stremio core normalizes announce URLs into `tracker:` sources and builds `PeerSearch::new(40, 200, ...)` (`stremio-core-development/src/types/streaming_server/request.rs:86`, `stremio-core-development/src/types/streaming_server/request.rs:91`, `stremio-core-development/src/types/streaming_server/request.rs:105`, `stremio-core-development/src/types/streaming_server/request.rs:106`).
- Perpetus has a built-in default tracker list (`stream-server-master/enginefs/src/lib.rs:42`, `stream-server-master/enginefs/src/lib.rs:53`) plus a manager that fetches, ranks, persists, and refreshes trackers (`stream-server-master/enginefs/src/trackers.rs:8`, `stream-server-master/enginefs/src/trackers.rs:10`, `stream-server-master/enginefs/src/trackers.rs:152`, `stream-server-master/enginefs/src/trackers.rs:168`, `stream-server-master/enginefs/src/trackers.rs:177`).
- Perpetus probes HTTP/UDP tracker reachability and sorts by RTT (`stream-server-master/enginefs/src/tracker_prober.rs:11`, `stream-server-master/enginefs/src/tracker_prober.rs:33`, `stream-server-master/enginefs/src/tracker_prober.rs:53`, `stream-server-master/enginefs/src/tracker_prober.rs:76`).
- Perpetus injects default trackers, adds final trackers, force-reannounces, force-DHT-announces, then background-ranks and replaces trackers (`stream-server-master/enginefs/src/backend/libtorrent/mod.rs:518`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:526`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:531`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:532`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:534`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:537`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:539`).
- STREAM_LIFECYCLE_FIX overlap: non-overlap.

Hypotheses:

- Hypothesis — Tankoban may be sufficient for magnets with rich add-on tracker lists, but less resilient for tracker-light or stale-tracker sources because it lacks ranked cached fallback trackers. Agent 4 to validate.
- Hypothesis — Active 0%-buffering reports should not be assigned to trackers from peer count alone; first-piece request/arrival and tracker-source metrics need to be separated. Agent 4 to validate.

### 8. Backend Abstraction (Multi-Implementation)

Observations:

- Tankoban Stream-A is libtorrent-specific through direct `StreamEngine` calls to `TorrentEngine` (`src/core/stream/StreamEngine.cpp:148`, `src/core/stream/StreamEngine.cpp:156`, `src/core/stream/StreamEngine.cpp:220`, `src/core/stream/StreamEngine.cpp:615`).
- Perpetus defines a backend trait for add/get/remove/list torrent and a handle trait for stats, file readers, file paths, prepare/clear streaming, and file listing (`stream-server-master/enginefs/src/backend/mod.rs:22`, `stream-server-master/enginefs/src/backend/mod.rs:37`, `stream-server-master/enginefs/src/backend/mod.rs:44`, `stream-server-master/enginefs/src/backend/mod.rs:51`, `stream-server-master/enginefs/src/backend/mod.rs:57`, `stream-server-master/enginefs/src/backend/mod.rs:60`).
- Perpetus includes both a libtorrent backend and a librqbit backend shape. The librqbit backend creates a session, restores torrents, and implements the common traits, though several methods are simplified or incomplete (`stream-server-master/enginefs/src/backend/librqbit.rs:13`, `stream-server-master/enginefs/src/backend/librqbit.rs:18`, `stream-server-master/enginefs/src/backend/librqbit.rs:97`, `stream-server-master/enginefs/src/backend/librqbit.rs:150`, `stream-server-master/enginefs/src/backend/librqbit.rs:198`).
- Perpetus' FFI layer exposes broad libtorrent controls including settings, add-torrent parameters, sequential mode, stats, file priorities, and direct memory storage (`stream-server-master/bindings/libtorrent-sys/src/lib.rs:1`, `stream-server-master/bindings/libtorrent-sys/src/lib.rs:11`, `stream-server-master/bindings/libtorrent-sys/src/lib.rs:54`, `stream-server-master/bindings/libtorrent-sys/src/lib.rs:75`, `stream-server-master/bindings/libtorrent-sys/cpp/memory_storage.cpp:1`, `stream-server-master/bindings/libtorrent-sys/cpp/memory_storage.cpp:18`).
- STREAM_LIFECYCLE_FIX overlap: non-overlap.

Hypotheses:

- Hypothesis — Backend abstraction is not required for near-term Tankoban parity, but the absence of an interface boundary makes memory-only storage, piece waiters, and tracker-policy experiments harder to isolate from `TorrentEngine`. Agent 4 to validate.
- Hypothesis — Any future abstraction would need an Agent 4 / Agent 4B ownership decision because Stream-A crosses the StreamEngine/TorrentEngine domain boundary. Agent 4 to validate.

### 9. Stats / Health Endpoints

Observations:

- Tankoban exposes stream status internally as peers and download speed (`src/core/stream/StreamEngine.cpp:347`, `src/core/stream/StreamEngine.cpp:353`, `src/core/stream/StreamEngine.cpp:354`) and has temporary stream-head-gate diagnostics (`src/core/stream/StreamEngine.cpp:595`, `src/core/torrent/TorrentEngine.cpp:1205`). The Stream-A source read shows no HTTP stats or health endpoint.
- Perpetus exposes `/heartbeat`, `/stats.json`, `/network-info`, per-engine stats, per-file stats, peers, device-info, and hwaccel profiler routes (`stream-server-master/server/src/main.rs:204`, `stream-server-master/server/src/main.rs:205`, `stream-server-master/server/src/main.rs:206`, `stream-server-master/server/src/main.rs:220`, `stream-server-master/server/src/main.rs:224`, `stream-server-master/server/src/main.rs:228`, `stream-server-master/server/src/main.rs:252`, `stream-server-master/server/src/main.rs:253`).
- Perpetus system routes return engine statistics, heartbeat success, network interfaces, and settings (`stream-server-master/server/src/routes/system.rs:15`, `stream-server-master/server/src/routes/system.rs:19`, `stream-server-master/server/src/routes/system.rs:43`, `stream-server-master/server/src/routes/system.rs:47`, `stream-server-master/server/src/routes/system.rs:143`). Settings cover cache, proxy, BT, remote HTTPS, transcode profile, and tracker-cache fields (`stream-server-master/server/src/routes/system.rs:61`, `stream-server-master/server/src/routes/system.rs:69`, `stream-server-master/server/src/routes/system.rs:71`, `stream-server-master/server/src/routes/system.rs:73`, `stream-server-master/server/src/routes/system.rs:87`, `stream-server-master/server/src/routes/system.rs:90`).
- Stremio core initializes streaming-server state by fetching settings, playback devices, network info, and device info (`stremio-core-development/src/models/streaming_server.rs:57`, `stremio-core-development/src/models/streaming_server.rs:59`, `stremio-core-development/src/models/streaming_server.rs:60`, `stremio-core-development/src/models/streaming_server.rs:62`, `stremio-core-development/src/models/streaming_server.rs:63`). It also has a statistics loadable and fetch path (`stremio-core-development/src/models/streaming_server.rs:53`, `stremio-core-development/src/models/streaming_server.rs:183`, `stremio-core-development/src/models/streaming_server.rs:194`).
- Stremio core statistics include files, sources, peer search, speeds, peers, stream length/name/progress, swarm size, and pause state (`stremio-core-development/src/types/streaming_server/statistics.rs:79`, `stremio-core-development/src/types/streaming_server/statistics.rs:82`, `stremio-core-development/src/types/streaming_server/statistics.rs:83`, `stremio-core-development/src/types/streaming_server/statistics.rs:85`, `stremio-core-development/src/types/streaming_server/statistics.rs:90`, `stremio-core-development/src/types/streaming_server/statistics.rs:94`, `stremio-core-development/src/types/streaming_server/statistics.rs:95`, `stremio-core-development/src/types/streaming_server/statistics.rs:100`).
- STREAM_LIFECYCLE_FIX overlap: partial diagnostic overlap only; no stable stats surface was added by that work.

Hypotheses:

- Hypothesis — Tankoban's current diagnostics may be enough for immediate Agent 4/4B triage, but missing stable stats endpoints make comparative validation and future audits harder. Agent 4 to validate.
- Hypothesis — A minimal internal stats surface could reduce reliance on temporary qDebug traces; per Rule 15 this should be Agent-readable telemetry rather than Hemanth-run grep work. Agent 4 to validate.

### 10. Add-On Protocol Surface Fulfilled By The Server

Observations:

- Tankoban stream representation carries `notWebReady`, `bingeGroup`, proxy headers, filename, video hash/size, stream subtitles, source kind, trackers, and file index (`src/core/stream/addon/StreamInfo.h:15`, `src/core/stream/addon/StreamInfo.h:20`, `src/core/stream/addon/StreamInfo.h:23`, `src/core/stream/addon/StreamInfo.h:24`, `src/core/stream/addon/StreamInfo.h:30`, `src/core/stream/addon/StreamSource.h:21`, `src/core/stream/addon/StreamSource.h:22`, `src/core/stream/addon/StreamSource.h:24`).
- Tankoban `StreamEngine::streamFile(addon::Stream)` converts magnets to local HTTP, returns direct URLs unchanged for HTTP/URL, and rejects YouTube (`src/core/stream/StreamEngine.cpp:71`, `src/core/stream/StreamEngine.cpp:81`, `src/core/stream/StreamEngine.cpp:84`, `src/core/stream/StreamEngine.cpp:86`, `src/core/stream/StreamEngine.cpp:95`, `src/core/stream/StreamEngine.cpp:100`).
- Stremio video accepts either `stream.url` magnets/direct URLs or `stream.infoHash`; magnet announces become `tracker:` sources and `createTorrent` returns server URLs (`stremio-video-master/src/withStreamingServer/convertStream.js:20`, `stremio-video-master/src/withStreamingServer/convertStream.js:33`, `stremio-video-master/src/withStreamingServer/convertStream.js:39`, `stremio-video-master/src/withStreamingServer/convertStream.js:61`, `stremio-video-master/src/withStreamingServer/convertStream.js:62`).
- Stremio video `createTorrent` uses `/{infoHash}/create` when peer search or file guessing is needed, and otherwise can build `/{infoHash}/{fileIdx}` directly (`stremio-video-master/src/withStreamingServer/createTorrent.js:18`, `stremio-video-master/src/withStreamingServer/createTorrent.js:19`, `stremio-video-master/src/withStreamingServer/createTorrent.js:41`, `stremio-video-master/src/withStreamingServer/createTorrent.js:55`, `stremio-video-master/src/withStreamingServer/createTorrent.js:68`).
- Stremio core defines `CreatedTorrent` with torrent, peer search, and optional file-index guessing semantics (`stremio-core-development/src/types/streaming_server/mod.rs:41`, `stremio-core-development/src/types/streaming_server/mod.rs:44`, `stremio-core-development/src/types/streaming_server/mod.rs:46`, `stremio-core-development/src/types/streaming_server/mod.rs:47`, `stremio-core-development/src/types/streaming_server/mod.rs:51`).
- Perpetus exposes create/remove/list, byte routes, subtitle routes, archives, local addon, proxy, YouTube, FTP, NZB, casting, HLS, and probe routes (`stream-server-master/server/src/main.rs:211`, `stream-server-master/server/src/main.rs:214`, `stream-server-master/server/src/main.rs:218`, `stream-server-master/server/src/main.rs:229`, `stream-server-master/server/src/main.rs:255`, `stream-server-master/server/src/main.rs:256`, `stream-server-master/server/src/main.rs:261`, `stream-server-master/server/src/main.rs:262`, `stream-server-master/server/src/main.rs:263`, `stream-server-master/server/src/main.rs:266`, `stream-server-master/server/src/main.rs:293`).
- Public webtor Stremio docs show generic stream items with `infoHash`, `fileIdx`, `url`, `ytId`, `externalUrl`, `behaviorHints`, and `sources`; behavior hints include `bingeGroup` and `filename` (https://pkg.go.dev/github.com/webtor-io/web-ui/services/stremio, fetched page lines 651-670). This is not a Torrentio-specific trace.
- STREAM_LIFECYCLE_FIX overlap: non-overlap.

Hypotheses:

- Hypothesis — Tankoban currently implements a narrower server contract than Stremio/perpetus, with more responsibility pushed into add-on parsing and native player layers. Agent 4 to validate whether that is intentional architecture or parity debt.
- Hypothesis — Missing `create`/guess-file semantics may matter less if Tankoban's add-on layer always resolves `fileIdx`/filename before StreamEngine; this needs real Torrentio movie/series traces before Slice C or 3b TODOs. Agent 4 to validate.

### 11. Other Substrate Observations

Observations:

- Tankoban returns `UNSUPPORTED_SOURCE` for YouTube at the StreamEngine layer (`src/core/stream/StreamEngine.cpp:100`, `src/core/stream/StreamEngine.cpp:103`). Perpetus nests a `/yt` router (`stream-server-master/server/src/main.rs:255`).
- Perpetus supports archive and NZB route families through server routes and bindings (`stream-server-master/server/src/main.rs:256`, `stream-server-master/server/src/main.rs:261`; `stream-server-master/bindings/async-rar/src/lib.rs:1`, `stream-server-master/bindings/async-rar/src/lib.rs:41`, `stream-server-master/bindings/async-sevenz/src/lib.rs:31`, `stream-server-master/bindings/async-sevenz/src/lib.rs:106`). Its local add-on can emit archive-backed streams with `notWebReady: false` and `bingeGroup` (`stream-server-master/server/src/local_addon/mod.rs:212`, `stream-server-master/server/src/local_addon/mod.rs:218`, `stream-server-master/server/src/local_addon/mod.rs:224`, `stream-server-master/server/src/local_addon/mod.rs:228`, `stream-server-master/server/src/local_addon/mod.rs:230`).
- Perpetus libtorrent backend uses memory-only storage for streaming, with disk caching separated after completion (`stream-server-master/enginefs/src/backend/libtorrent/mod.rs:72`, `stream-server-master/enginefs/src/backend/libtorrent/mod.rs:74`, `stream-server-master/bindings/libtorrent-sys/cpp/memory_storage.cpp:1`, `stream-server-master/bindings/libtorrent-sys/cpp/memory_storage.cpp:93`). Tankoban serves via `QFile` from the libtorrent save path (`src/core/stream/StreamHttpServer.cpp:268`, `src/core/stream/StreamHttpServer.cpp:269`, `src/core/stream/StreamHttpServer.cpp:347`).
- STREAM_LIFECYCLE_FIX overlap: mostly non-overlap.

Hypotheses:

- Hypothesis — Archive/YouTube/NZB support should remain outside Slice A main TODOs unless Agent 4 defines them as stream-server substrate parity goals; they likely belong to later add-on/source slices. Agent 4 to validate.
- Hypothesis — Memory-only storage is the deepest architectural divergence because it changes piece availability, read latency, cache semantics, and cancellation hazards together. Agent 4 to validate.

## Cross-Cutting Observations

- Perpetus treats the streaming server as a broad contract surface, not only a byte pipe: routes include stats, create, probe, HLS, subtitles, proxy, archives, local addon, casting, and settings (`stream-server-master/server/src/main.rs:202`, `stream-server-master/server/src/main.rs:211`, `stream-server-master/server/src/main.rs:238`, `stream-server-master/server/src/main.rs:255`, `stream-server-master/server/src/main.rs:266`).
- Tankoban treats Stream-A as a narrow local HTTP bridge from TorrentEngine to the native sidecar. That can be valid architecture, but deliberate omissions should be labeled before downstream slices audit player/source-picker behavior.
- Perpetus' core byte-serving loop is event-driven and memory-first: piece-finished alerts populate a piece cache, piece waiters wake exact readers, and readers update priorities as offsets change (`stream-server-master/enginefs/src/backend/libtorrent/mod.rs:195`, `stream-server-master/enginefs/src/piece_waiter.rs:32`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:183`, `stream-server-master/enginefs/src/backend/libtorrent/stream.rs:317`).
- Tankoban's loop is disk-backed, polling-based, and readiness-gated: it waits for 5 MB contiguous head before playback and polls `haveContiguousBytes` every 200 ms per chunk (`src/core/stream/StreamEngine.cpp:270`, `src/core/stream/StreamHttpServer.cpp:77`, `src/core/stream/StreamHttpServer.cpp:88`, `src/core/stream/StreamHttpServer.cpp:102`).
- Both systems use libtorrent deadlines and file priorities. The divergence is initial target size, offset-driven retargeting, evented waiting, memory-vs-disk read path, tracker/cache support, and route breadth.
- STREAM_LIFECYCLE_FIX aligns Tankoban with the reference in the cancellation/lifecycle class, but it does not cover startup heuristics, cache, HLS, stats, or tracker ranking.

## Priority Ranking

P0 - New / non-overlap:

1. Startup can remain blocked behind Tankoban's 5 MB contiguous head target while the perpetus reference starts from 1 MB / one to two urgent pieces and expands dynamically. This maps to "stuck buffering before first frame." It is non-overlap with STREAM_LIFECYCLE_FIX and needs validation against the current 0%-buffering traces.

P0 - Resolved-by-prior-work / not reopened:

1. Close/source-switch cancellation while HTTP workers wait on pieces appears addressed by token set-before-erase and pre-`haveContiguousBytes` checks (`src/core/stream/StreamEngine.cpp:294`, `src/core/stream/StreamHttpServer.cpp:89`). This overlaps STREAM_LIFECYCLE_FIX and is not a new finding from this audit.

P1:

1. Missing HLS/probe/transcoding substrate compared with Stremio/perpetus.
2. No memory-first piece cache or disk cache retention comparable to perpetus.
3. Tracker discovery lacks perpetus-style ranked cached public trackers and replacement.
4. Stats/health observability is minimal compared with Stremio/perpetus.

P2:

1. HTTP route compatibility is narrower (`/stream/{hash}/{file}` only; no bare `/{hash}/{file}` pattern).
2. Subtitle server endpoints are absent in Stream-A, though Tankoban may intentionally keep this in sidecar/player layers.
3. Create/guess-file protocol surface is narrower than Stremio/perpetus, pending real add-on trace validation.

P3:

1. Backend abstraction and archive/YouTube/NZB support are strategic parity gaps rather than immediate Stream-A correctness defects unless Agent 4 expands the goal.
2. Multi-range HTTP support is not a perpetus divergence and should stay low priority unless a concrete client emits multi-range requests.

## Reference Comparison Matrix

| Axis | Tankoban Current | perpetus `stream-server-master` | Stremio Consumer Pattern | Slice-A Assessment |
| --- | --- | --- | --- | --- |
| Startup gate | 5 MB contiguous head before `readyToStart` (`StreamEngine.cpp:209`, `StreamEngine.cpp:270`) | 1 MB / 1-2 startup pieces, 0 ms deadlines (`priorities.rs:6`, `handle.rs:270`) | Consumer accepts converted server URL; HLS probe may read media URL (`withStreamingServer.js:134`) | New P0 risk to validate |
| Piece wait | 200 ms polling, 15s timeout per 256 KB chunk (`StreamHttpServer.cpp:77`, `StreamHttpServer.cpp:88`) | Waker registry and alert-driven cache wake (`piece_waiter.rs:27`, `mod.rs:245`) | Not specified at consumer level | Architectural divergence |
| Head/tail metadata | Head 5 MB deadlines; no explicit tail model in Stream-A (`StreamEngine.cpp:589`) | Defers tail metadata and preserves head on metadata seeks (`handle.rs:322`, `stream.rs:92`) | HLS probe expects server to probe containers | P1/P2 depending sidecar needs |
| HTTP 206 | Single range, 416 with `Content-Range`, fixed headers (`StreamHttpServer.cpp:216`, `StreamHttpServer.cpp:241`) | Single range, both `/stream` and bare routes (`stream.rs:253`, `main.rs:229`) | Builds `/{infoHash}/{fileIdx}` direct URLs (`createTorrent.js:11`) | P2 route compatibility |
| Cache | Disk-backed libtorrent files, delete on stop (`StreamEngine.cpp:324`) | Memory-first piece cache, optional disk persistence, 5-min idle engine cleanup (`piece_cache.rs:61`, `disk_cache.rs:41`, `lib.rs:140`) | Settings include cache size (`settings.rs:19`) | P1 |
| Cancellation | Atomic token before erase/remove; HTTP checks before engine call (`StreamEngine.cpp:304`, `StreamHttpServer.cpp:99`) | Drop-based stream guard and delayed cleanup (`stream.rs:39`, `lib.rs:330`) | Load/unload guards stale async results (`withStreamingServer.js:194`) | Closed overlap |
| Subtitles | Add-on subtitle data carried, no server VTT route in Stream-A (`StreamInfo.h:36`) | `/subtitlesTracks`, VTT proxy, embedded text extraction (`main.rs:238`, `subtitles.rs:74`, `engine.rs:431`) | Rewrites subtitles through `/subtitles.vtt` (`withStreamingServer.js:177`) | P2 / Slice D |
| HLS/transcode | None in Stream-A | `/hlsv2`, probe, ffmpeg segment transcoding, hwaccel (`main.rs:266`, `hls.rs:445`, `hwaccel.rs:168`) | Uses HLS when direct play unsupported (`withStreamingServer.js:151`) | P1 strategic |
| Trackers/DHT | DHT/LSD/UPnP, fixed bootstrap, add-on trackers (`TorrentEngine.cpp:190`, `StreamSource.h:72`) | Default trackers, cached ranked manager, force reannounce/DHT (`lib.rs:42`, `trackers.rs:168`, `mod.rs:531`) | PeerSearch 40/200 with DHT + trackers (`createTorrent.js:31`) | P1 |
| Backend abstraction | Direct `StreamEngine` -> `TorrentEngine` | `TorrentBackend`/`TorrentHandle`, libtorrent + librqbit shape (`backend/mod.rs:22`) | Consumer sees server contract | P3 |
| Stats/health | Internal peers/dlSpeed only (`StreamEngine.cpp:347`) | `/stats.json`, `/heartbeat`, `/network-info`, device/hwaccel, file stats (`main.rs:204`) | Core fetches settings/network/device/stats (`streaming_server.rs:59`) | P1/P2 |
| Protocol breadth | Magnet/direct URL/HTTP; YouTube unsupported (`StreamEngine.cpp:86`, `StreamEngine.cpp:100`) | Create/remove/list, proxy, archives, local addon, YouTube, FTP, NZB, HLS (`main.rs:211`, `main.rs:255`) | convertStream/createTorrent expect broader server | P2/P3 |

## Implementation Strategy Options For Agent 4 To Choose Between

Option A - Keep Tankoban's native-sidecar contract narrow and document deliberate non-goals.

- Trade-off: lowest substrate churn and least cross-agent scope expansion.
- Benefit: preserves the recently stabilized lifecycle path.
- Cost: later slices must treat HLS, subtitle proxy, server stats, create/guess, archives, and route compatibility as intentional gaps or separate roadmap items.

Option B - Target startup reliability only in Stream-A.

- Trade-off: narrowest user-impact work while staying inside byte-serving scope.
- Candidate decision areas: startup gate size, dynamic first-piece deadlines, evented/piece-finished waits, tail metadata handling, and tracker discovery.
- Cost: cache/HLS/stats parity remains unresolved.

Option C - Adopt a reference-style substrate tier.

- Trade-off: larger architectural shift toward a Stremio-compatible local server.
- Candidate inclusions: bare hash routes, `/create`, stats/health, memory-first piece cache, tracker ranking, HLS/probe, subtitle VTT routes.
- Cost: crosses Agent 4/4B boundaries and increases test burden; should be split into narrow TODOs if chosen.

Option D - Hybrid: codify current native behavior, then add observability before behavior changes.

- Trade-off: delays parity fixes but improves evidence.
- Candidate surfaces: first-piece deadline/request/arrival logs, current prioritized piece range, active file index, tracker count/source, cache reuse, and cancellation state.
- Cost: may feel indirect while startup blockage is user-visible, but reduces root-cause guesswork.

Per Rule 14, these are choices for Agent 4 (and Agent 0 for TODO shaping), not Hemanth.

## Cross-Slice Findings Appendix

Slice D (player UX / buffering / subtitles):

- Stremio video rewrites unsupported streams to HLS and rewrites subtitles through the streaming server (`stremio-video-master/src/withStreamingServer/withStreamingServer.js:151`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:172`, `stremio-video-master/src/withStreamingServer/withStreamingServer.js:177`). If Tankoban keeps HLS/subtitle routing out of Stream-A, Slice D should not file those as player-only bugs.
- Tankoban's socket-close-on-piece-timeout relies on native sidecar retry/stall-buffering behavior described in comments (`src/core/stream/StreamHttpServer.cpp:317`, `src/core/stream/StreamHttpServer.cpp:321`). Slice D should validate the user-visible behavior after substrate validation.

Slice 3a (library/progress/continue notifications):

- Cache retention affects whether a just-watched or partially watched stream reopens quickly. Perpetus has 5-minute engine retention and optional disk persistence (`stream-server-master/enginefs/src/lib.rs:140`, `stream-server-master/enginefs/src/disk_cache.rs:41`); Tankoban deletes on stream stop (`src/core/stream/StreamEngine.cpp:324`).
- Stremio statistics include `streamProgress`, `streamName`, and file stats (`stremio-core-development/src/types/streaming_server/statistics.rs:95`, `stremio-core-development/src/types/streaming_server/statistics.rs:97`). Tankoban Stream-A does not expose equivalent server stats.

Slice C (metadetails/source picker):

- `behaviorHints.bingeGroup`, `notWebReady`, filename, proxy headers, and source ordering need real add-on traces. Tankoban structs carry those fields (`src/core/stream/addon/StreamInfo.h:15`, `src/core/stream/addon/StreamInfo.h:23`), but this audit did not verify real Torrentio ordering.

Slice 3b (add-ons/discover/search):

- Stremio/createTorrent peer-search semantics (`dht:<hash>` plus tracker sources, min 40, max 200) are partly add-on/source interpretation and partly Stream-A (`stremio-video-master/src/withStreamingServer/createTorrent.js:31`, `stremio-video-master/src/withStreamingServer/createTorrent.js:36`). Downstream audit should avoid duplicating tracker findings already logged here.

Slice 3c (settings/add-ons UI):

- Perpetus/Stremio expose settings for cache size, proxy streams, BT connection/timeouts, remote HTTPS, and transcode profile (`stream-server-master/server/src/routes/system.rs:61`, `stream-server-master/server/src/routes/system.rs:69`, `stream-server-master/server/src/routes/system.rs:87`). If Tankoban adds substrate settings, 3c owns UI exposure.

## Gaps Agent 4 Should Close In Validation

1. Capture one clean cold-start log for a healthy-swarm magnet showing metadata ready, file selection, head deadline range, piece 0 request/finish timing, `contiguousBytesFromOffset` samples, and first local HTTP request.
2. For the same magnet, record whether 5 MB contiguous head is required by the sidecar probe in practice, or whether playback can begin reliably with a smaller head target plus HTTP piece waits.
3. Validate whether tail metadata reads occur before first frame for MP4/MKV/WebM in Tankoban's sidecar path, and whether those reads compete with head deadlines.
4. Run close-while-buffering stress and confirm the expected signature is `piece wait cancelled` rather than stale-handle error, crash, or long hang.
5. Re-open the same stream immediately after stop and measure whether data is reused or fully discarded.
6. Test one weak/tracker-light magnet with add-on trackers removed or minimized, then compare peer discovery and head-piece timing against a rich-tracker magnet.
7. Capture a real Torrentio movie and series stream response through Tankoban's add-on path, including `behaviorHints`, `sources`, `infoHash`, `fileIdx`, `url`, and ordering.
8. Exercise direct URL streams with proxy headers, if any add-on provides them, and verify whether Tankoban intentionally bypasses server proxy behavior.
9. For incompatible-codec samples, record whether failure is handled by the native sidecar or whether a Stremio-like HLS fallback would be required for parity.
10. Decide which stable stats are needed for future audits: at minimum first-piece availability, prioritized piece range, active file index, tracker count/source, peers, download speed, and cancellation state.

Per Rule 15, these validation items are framed for Agent 4/Agent 4B logs and empirical substrate observation. Hemanth should only be asked for UI-level smoke where UI perception is the missing input.

## Audit Boundary Notes

- I did not compile or run Tankoban or the reference server.
- I did not modify `src/` or any non-audit file while producing this report.
- I did not find a clean public real Torrentio `/stream/series/<id>.json` trace. Torrentio-specific behavior hints and ordering remain validation gaps.
- I did not assert root causes for active 0%-buffering reports. Startup gate, piece priority, tracker behavior, and cache/read path are comparative hypotheses for Agent 4 to validate.
- I did not review other agents' shipped code as a code review. STREAM_LIFECYCLE_FIX references are only overlap/non-overlap boundaries.
- Some perpetus code is explicitly optimized and commented in its own repository. This report cites what the code does, not whether every perpetus choice is correct for Tankoban.
