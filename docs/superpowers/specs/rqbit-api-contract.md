# rqbit API Contract — captured ground truth (Phase 1, Task 1)

- **Date:** 2026-06-07
- **Captured by:** Agent 4 (Opus) on THIS machine against the real binary
- **Binary:** `rqbit 8.1.1` (latest *stable*; v9.0.0-beta.2 was rejected as a beta-named release despite GitHub's `prerelease:false` flag)
- **Source:** <https://github.com/ikatson/rqbit/releases/download/v8.1.1/rqbit.exe>
- **File:** `resources/rqbit/rqbit.exe` — PE32+ console exe, x86-64, ~10 MB (gitignored; build-deployed beside `Tankoban.exe`, mirroring the ffmpeg sidecar)
- **Purpose:** lock the exact CLI + JSON shapes so Tasks 2–8 are written against reality, not assumptions.

---

## 1. Launch (headless server)

```
rqbit.exe --http-api-listen-addr 127.0.0.1:<port> [-v info] server start <OUTPUT_FOLDER>
```

- `--http-api-listen-addr <ip:port>` is a **TOP-LEVEL global option** (comes *before* the `server` subcommand). Default `127.0.0.1:3030`. Env: `RQBIT_HTTP_API_LISTEN_ADDR`.
- `<OUTPUT_FOLDER>` is the **positional** arg of `server start` (created if missing) — this is the download/staging dir.
- Useful flags for our embedding:
  - `--disable-persistence` — don't read/write session state to disk (env `RQBIT_SESSION_PERSISTENCE_DISABLE`). Consider for a clean per-launch Theatre session.
  - `--persistence-location <dir>` — where session JSON lives (env `RQBIT_SESSION_PERSISTENCE_LOCATION`). **Note:** default DHT cache + session live under `%APPDATA%\rqbit\` and `%LOCALAPPDATA%\rqbit\` — set these explicitly if we want them inside Tankoban's data dir.
  - `--disable-dht-persistence` — needed if we ever run **multiple** rqbit instances (DHT port would otherwise conflict).
  - `-v info` — console log level (trace|debug|info|warn|error). `--log-file <path>` to also tee to a file.
- On start it logs: `starting HTTP API at http://127.0.0.1:<port>` — our `RqbitProcess` health-check polls `GET /torrents` until 200 (observed ready in <1s).

---

## 2. Endpoint index (`GET /` — verbatim from the running 8.1.1 server)

| Method + path | Purpose |
|---|---|
| `POST /torrents` | Add a torrent — body is `magnet:` / `http://` / a local file path |
| `GET  /torrents` | List torrents (id + info_hash + name + output_folder) |
| `GET  /torrents/{id_or_infohash}` | Torrent details (incl. files) |
| `GET  /torrents/{id_or_infohash}/stats/v1` | Torrent stats (progress/state) |
| `GET  /torrents/{id_or_infohash}/stream/{file_idx}` | **Stream a file. Accepts Range header to seek.** |
| `POST /torrents/{id_or_infohash}/pause` | Pause |
| `POST /torrents/{id_or_infohash}/start` | Resume |
| `POST /torrents/{id_or_infohash}/delete` | Forget torrent **+ remove files** |
| `POST /torrents/{id_or_infohash}/forget` | Forget torrent **keep files** ← offline-promotion (Phase 2) |
| `POST /torrents/{id_or_infohash}/update_only_files` | `{"only_files":[0,1,2]}` — select which files to download (could fetch only the wanted episode) |
| `POST /torrents/resolve_magnet` | Magnet → torrent file bytes |
| `GET  /torrents[/{id}]/playlist` | M3U8 playlist |
| `GET  /stats`, `/dht/stats`, `/dht/table` | Session/DHT stats |

All `{id_or_infohash}` endpoints accept **either** the numeric id **or** the 40-char infohash — robust to either.

---

## 3. Add a torrent — `POST /torrents`

- **Body:** the raw magnet string (`Content-Type` irrelevant; posted with `--data-raw`). Confirmed working: raw body. (Query params like `?overwrite=true&only_files=...` are also accepted but not needed for Phase 1.)
- **Response (verbatim, trimmed file list):**

```json
{
  "id": 0,
  "details": {
    "id": 0,
    "info_hash": "08ada5a7a6183aae1e09d831df6748d566095a10",
    "name": "Sintel",
    "output_folder": "C:/temp/rqbit_probe\\Sintel",
    "files": [
      {"name":"Sintel.en.srt","components":["Sintel.en.srt"],"length":1514,"included":true,"attributes":{"symlink":false,"hidden":false,"padding":false,"executable":false}},
      {"name":"Sintel.mp4","components":["Sintel.mp4"],"length":129241752,"included":true,"attributes":{...}},
      {"name":"poster.jpg","components":["poster.jpg"],"length":46115,"included":true,"attributes":{...}}
    ]
  },
  "output_folder": "C:/temp/rqbit_probe\\Sintel",
  "seen_peers": null
}
```

**Field map (what Tasks 2/3/5 use):**
- **Torrent id** → top-level **`id`** (integer; e.g. `0`). Also at `details.id`. `streamUrl`/`stats`/`delete` use this id (string-ified).
- **File list** → **`details.files[]`** — each object has:
  - `name` (string — filename; full relative path for nested torrents, also split in `components[]`)
  - `length` (int64 — byte size)
  - `included` (bool), `attributes{}` (symlink/hidden/padding/executable)
  - **index = array position** → that is the `{file_idx}` for the stream URL.
- The add response returns immediately once metadata resolves (Sintel ~instant via trackers/DHT). For a fresh magnet with no metadata yet, expect a short wait.

---

## 4. Stats — `GET /torrents/{id}/stats/v1`

```json
{
  "state": "live",
  "file_progress": [1652, 1514, 1554, 1618, 1546, 129241752, 1537, 1536, 1551, 2016, 46115],
  "error": null,
  "progress_bytes": 129302391,
  "uploaded_bytes": 0,
  "total_bytes": 129302391,
  "finished": true,
  "live": {
    "snapshot": { "downloaded_and_checked_bytes": 129302391, "peer_stats": { "live": 0, "seen": 254, ... }, ... },
    "download_speed": { "mbps": 0.0, "human_readable": "0.00 MiB/s" },
    "upload_speed": { "mbps": 0.0, "human_readable": "0.00 MiB/s" },
    "time_remaining": null
  }
}
```

**Field map (`RqbitStats::parseStats`, Task 2):**
- `state` (string) — e.g. `"live"`, `"initializing"`, `"paused"`, `"error"`.
- `total_bytes` (int64) — total size of selected files.
- `progress_bytes` (int64) — bytes downloaded so far.
- `finished` (bool).
- `file_progress[]` (int64 array, indexed by file) — **per-file** downloaded bytes; index aligns with `details.files[]`. (Bonus — useful for per-episode % later; not needed for the Phase-1 `RqbitStats` struct.)
- `error` (string|null), `live.*` (live download/peer telemetry; null when paused/finished-idle).

> **Reconciliation with the plan:** the plan's `parseStats` reads `state` / `total_bytes` / `progress_bytes` / `finished` — **all four match the real API verbatim. No parser change needed.** Task 2 fixture should use these real keys.

---

## 5. Stream — `GET /torrents/{id}/stream/{file_idx}`  ⚑ critical for seeking

Behavior verified against the fully-downloaded Sintel.mp4 (idx 5, 129 241 752 bytes):

| Request | Response | Body |
|---|---|---|
| **No Range** | `200 OK` · `accept-ranges: bytes` · `content-type: video/mp4` · `transfer-encoding: chunked` | full file |
| **Open-ended `Range: bytes=1000000-`** (← what ffmpeg/players send on seek) | **`206 Partial Content`** · `content-range: bytes 1000000-129241751/129241752` · `content-length: 128241752` | bytes 1000000→EOF ✓ |
| Closed `Range: bytes=1000000-1001023` | `200 OK` · chunked (range **ignored**, served full from byte 0) | full file ⚠ |

**Takeaway:** rqbit honors the **open-ended `bytes=START-`** form with a proper `206 + Content-Range` — which is exactly what the ffmpeg sidecar emits when seeking, so **seeking works**. It does NOT honor a *closed* sub-range (`bytes=N-M`) — returns 200 + full — but no media player issues closed sub-ranges for seeks, so this quirk is harmless. `content-type` is sniffed by rqbit (returned `video/mp4`). The sidecar already plays HTTP URLs with range seeking (it served the old Stremio stream-server), so the player side is proven.

---

## 6. Lifecycle / teardown

- **Pause/resume:** `POST /torrents/{id}/pause` · `POST /torrents/{id}/start`.
- **Stop streaming, drop everything:** `POST /torrents/{id}/delete` (removes the partially/fully downloaded files too).
- **Stop streaming, KEEP files:** `POST /torrents/{id}/forget` — the hook for **offline promotion** (Phase 2: a streamed title marked "keep offline" = `forget` + adopt the on-disk file into the library, no re-fetch).
- Process shutdown: `RqbitProcess::stop()` → `terminate()` then `kill()` after grace. (No orphaned `rqbit.exe` after teardown — verify in T9.)

---

## 7. Net effect on the plan

- `parseStats` field names: **confirmed, no change.**
- add-response `id` + `details.files[]` (`name`/`length`): **confirmed, no change.**
- `pickPrimaryVideoFile` reads `name` + `length`: **confirmed, no change.**
- Stream-URL `…/stream/{idx}` with the numeric id + the open-ended-range seek path: **confirmed working (206).**
- Launch shape: `--http-api-listen-addr` is a **pre-subcommand global** — `RqbitProcess` must place it before `server start <dir>` (the plan's arg order is correct).
