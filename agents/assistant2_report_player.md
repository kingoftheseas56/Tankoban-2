# Assistant 2 — Player Popover + Sidecar Protocol Reconnaissance Report
**Date:** 2026-03-25
**Scope:** filter_popover.py, track_popover.py, protocol.py, transport.py, session.py, client.py

---

## 1. filter_popover.py

### Widget Type and Dimensions
- Class: `FilterPopover(QFrame)`
- Object name: `"FilterPopover"`
- Min width: 220px
- Max width: 320px (`_MAX_WIDTH = 320`)
- Height: determined by `sizeHint()` based on content

### Positioning
- Anchored via `_anchor_above(anchor: QWidget)` — called with the chip button that opens it
- Anchor point: chip's **top-right** corner mapped to parent coordinates
- X: `max(0, anchor_pos.x() - popup_width)`  — right-aligned under chip top-right
- Y: `max(0, anchor_pos.y() - popup_height - 8)` — 8px gap above chip

### Background Styling
- Background: `BG_PANEL` (theme constant — opaque dark panel color)
- Border: `1px solid BORDER_DEFAULT`
- Border-radius: `8px`
- No shadow (no `QGraphicsDropShadowEffect`)
- StyleSheet is scoped: `QFrame#FilterPopover { ... }`

### Layout
- `QVBoxLayout`, margins `10,10,10,10`, spacing `6`

### Controls (in order)

#### Video Section header
- `QLabel("Video")` — color: `ACCENT_WARM`, font-size: 11px, font-weight: 700

#### Deinterlace checkbox
- Widget: `QCheckBox("Deinterlace")`
- Default: **unchecked**
- Active filter count: +1 if checked
- FFmpeg filter: `"yadif=mode=0"` appended to video filter string when checked

#### Brightness slider
- Widget: `QSlider(Horizontal)` + `QLabel` name (fixed 62px) + `QLabel` value (fixed 32px)
- Range: -100 to 100 (integer)
- Default value: **0** (maps to 0.0 float)
- Display format: `value / 100.0` with 1 decimal (e.g. "0.0", "-0.5", "1.0")
- Active filter count: +1 if value != 0
- FFmpeg contribution: `eq=brightness={b:.2f}:contrast={c:.2f}:saturation={s:.2f}` (combined eq filter)

#### Contrast slider
- Widget: `QSlider(Horizontal)`
- Range: 0 to 200
- Default value: **100** (maps to 1.0 float)
- Active filter count: +1 if value != 100
- FFmpeg contribution: part of `eq=...` combined filter

#### Saturation slider
- Widget: `QSlider(Horizontal)`
- Range: 0 to 200
- Default value: **100** (maps to 1.0 float)
- Active filter count: +1 if value != 100
- FFmpeg contribution: part of `eq=...` combined filter

#### Horizontal divider
- `QFrame(HLine)`, fixed height 1px, color: `BORDER_SUBTLE`

#### Audio Section header
- `QLabel("Audio")` — same style as Video header

#### Volume Normalization checkbox
- Widget: `QCheckBox("Volume Normalization")`
- Default: **unchecked**
- Active filter count: +1 if checked
- FFmpeg audio filter: `"loudnorm=I=-16"` when checked, else `""` (empty string)

### "Filters (N)" Chip Label Logic
- `active_filter_count()` returns count of non-default filters
- Counts: deinterlace (if checked) + brightness (if != 0) + contrast (if != 100) + saturation (if != 100) + normalization (if checked)
- Max possible: 5
- Chip label is managed externally — this method just returns the integer count

### What Command Is Sent
- Signal: `filter_changed = pyqtSignal(str, str)` — `(video_filter_spec, audio_filter_spec)`
- No direct sidecar command from this class — emits `filter_changed` signal, caller sends `set_filters` command
- Debounced: 300ms `QTimer` single-shot before emitting
- `build_video_filter()` returns comma-joined ffmpeg filter string (e.g. `"yadif=mode=0,eq=brightness=-0.50:contrast=1.20:saturation=0.80"`)
- `build_audio_filter()` returns `"loudnorm=I=-16"` or `""`
- eq filter only included if at least one of brightness/contrast/saturation is non-default

### Dismiss Behavior
- Global `QApplication.installEventFilter(self)` on open
- `eventFilter`: any `MouseButtonPress` outside the popover rect → `_dismiss()` (hide + remove filter)
- Re-click chip: `toggle()` calls `_dismiss()` if already visible
- No Escape key handler in the popover itself (would need to be in parent)
- No animation on open/close — instant `show()` / `hide()`

### HUD Integration
- `enterEvent` → calls `owner._on_hud_enter()` to prevent HUD auto-hide
- `leaveEvent` → calls `owner._on_hud_leave()`
- Owner resolved via `parent._player_owner` or parent itself

### State Restoration
- `set_state(deinterlace, brightness, contrast, saturation, normalization)` — sets all controls with signals blocked
- All params are float scale (brightness=0.0, contrast=1.0, saturation=1.0 = defaults)
- Internally multiplied by 100 to get slider integer values

---

## 2. track_popover.py

### Widget Type and Dimensions
- Class: `TrackPopover(QFrame)`
- Object name: `"TrackPopover"`
- Min width: 200px
- Max width: 320px
- Height: dynamic based on track count

### Positioning
- Identical anchor logic to FilterPopover: `_anchor_above(anchor)` uses chip's top-right
- X: `max(0, anchor_top_right.x() - popup_width)`
- Y: `max(0, anchor_top_right.y() - popup_height - 8)`

### Background Styling
- Background: `BG_PANEL`
- Border: `1px solid BORDER_DEFAULT`
- Border-radius: `8px`
- Scoped: `QFrame#TrackPopover { ... }`
- No shadow

### Layout
- `QVBoxLayout`, margins `10,10,10,10`, spacing `6`

### Audio Tracks Section
- Header: `QLabel("Audio")` — `ACCENT_WARM`, 11px, bold
- Widget: `QListWidget` (styled with `TRACK_LIST_SS`)
- Horizontal scrollbar: always off; vertical: as needed
- Track label construction from each track dict:
  - Priority: title → lang.upper() → "Track {id}"
  - If codec present: appended as "(codec)" suffix
  - Example: `"Japanese (aac)"` or `"Track 2 (ac3)"`
- Selection: `item.setSelected(True)` for `tid == current_aid` — QListWidget row highlight (not radio buttons, not checkmarks — it is list selection highlighting)
- On click: emits `audio_selected = pyqtSignal(int)` with track id, then calls `_dismiss()`
- Max visible rows: 4 (`_MAX_VISIBLE_ROWS = 4`)
- Row height: 30px (`_ROW_HEIGHT = 30`)
- List height: `max(row_count, 1) * 30 + 4`
- No "No audio tracks" empty state text — list simply shows 0 rows if empty

### Subtitle Tracks Section
- Header: `QLabel("Subtitle")` — same style
- Widget: `QListWidget` (same style as audio)
- Same label construction logic
- **"Off" item prepended at index 0** with `tid = 0`
- "Off" selected when `not sub_visible` or `current_sid <= 0`
- On click: emits `subtitle_selected = pyqtSignal(int)` with track id (0 for Off), then `_dismiss()`
- Max 4 visible rows; includes "Off" in count

### Sub Delay Control
- Row: `QHBoxLayout` with three buttons + center label
- Minus button (`QPushButton("−")`, fixed 28×24px): emits `sub_delay_adjusted(-100)` — delta of **-100ms**
- Plus button (`QPushButton("+")`, fixed 28×24px): emits `sub_delay_adjusted(100)` — delta of **+100ms**
- Reset button (`QPushButton("Reset")`, fixed 44×24px): emits `sub_delay_adjusted(0)` — **0 = reset signal**
- Center label: `"0ms"` default; `set_delay(ms)` updates it to `"{ms:+d}ms"` (e.g. "+100ms", "-300ms")
- Signal: `sub_delay_adjusted = pyqtSignal(int)` — delta ms, 0 means reset
- Step size: **100ms** per click
- No min/max enforced in the widget; caller enforces

### Sub Style Section
- Divider, then header `QLabel("Style")`
- Font size slider: range 16–48, default **24**; updates `_font_size_val` label
- Margin slider: range 0–100, default **40**; updates `_margin_val` label
- Outline checkbox: `QCheckBox("Outline")`, default **checked**
- Signal: `sub_style_changed = pyqtSignal(int, int, bool)` — (font_size, margin_v, outline)
- Debounced: 300ms single-shot timer before emit
- `set_style(font_size, margin_v, outline)` — sets values without emitting

### Dismiss Behavior
- Same global event filter pattern as FilterPopover
- `MouseButtonPress` outside rect → hide
- Re-click chip → `toggle()` → `_dismiss()`
- No animation

### HUD Integration
- Same `enterEvent`/`leaveEvent` → `_on_hud_enter`/`_on_hud_leave` pattern

---

## 3. protocol.py

### Transport Framing
- **One JSON object per line** — newline-delimited JSON (JSON-Lines / NDJSON)
- Commands sent to sidecar stdin; events read from sidecar stdout
- Max line size: **2 MiB** (`MAX_LINE_BYTES = 2 * 1024 * 1024`) — oversized lines are dropped
- Encoding: UTF-8, `ensure_ascii=True`, compact separators `(",", ":")`
- Each message ends with `"\n"` (LF)

### Command Envelope (app → sidecar)
Every command has this exact structure:
```json
{
  "type": "cmd",
  "seq": 42,
  "sessionId": "s_1742900000000_3",
  "name": "open",
  "payload": { ... }
}
```
- `type`: always `"cmd"` (string)
- `seq`: positive integer, monotonically increasing global counter (thread-safe)
- `sessionId`: string, format `"{prefix}_{timestamp_ms}_{generation}"` (e.g. `"s_1742900000000_3"`)
- `name`: command name string
- `payload`: dict (empty dict `{}` if no payload)

### Event Envelope (sidecar → app)
Every event has this structure:
```json
{
  "type": "evt",
  "name": "time_update",
  "sessionId": "s_1742900000000_3",
  "seqAck": 42,
  "payload": { ... }
}
```
- `type`: always `"evt"` (string) — required
- `name`: event name string — required, must be a known event
- `sessionId`: string (may be empty `""` for session-independent events like heartbeat)
- `seqAck`: integer or null — when present, acknowledges a specific command by its seq number
- `payload`: dict (optional field, may be absent)

### Required event envelope fields: `"type"` and `"name"` only

### All Known Commands (with payload fields)

#### `open`
```json
{
  "name": "open",
  "payload": {
    "path": "/path/to/video.mkv",      // required: non-empty string
    "startSeconds": 0.0,                // optional: float >= 0
    "videoId": "some-id",              // optional: string
    "showId": "some-show-id"           // optional: string
  }
}
```
- Timeout: **15.0s** (`OPEN_CMD_TIMEOUT_SEC`)
- After ack: first_frame must arrive within another **10.0s** (`FIRST_FRAME_DEADLINE_SEC`)

#### `ping`
```json
{ "name": "ping", "payload": {} }
```
- Timeout: 5.0s

#### `shutdown`
```json
{ "name": "shutdown", "payload": {} }
```
- Timeout: 5.0s

#### `pause`
```json
{ "name": "pause", "payload": {} }
```
- Timeout: 5.0s

#### `resume`
```json
{ "name": "resume", "payload": {} }
```
- Timeout: 5.0s

#### `stop`
```json
{ "name": "stop", "payload": {} }
```
- Timeout: 5.0s; sidecar stops audio/video decoding, emits `closed` event; process stays alive

#### `seek`
```json
{
  "name": "seek",
  "payload": {
    "positionSec": 123.456   // required: finite float >= 0
  }
}
```
- Timeout: 5.0s

#### `set_rate`
```json
{
  "name": "set_rate",
  "payload": {
    "rate": 1.5   // required: float in [0.0625, 16.0], finite
  }
}
```
- `RATE_MIN = 0.0625` (1/16x), `RATE_MAX = 16.0` (16x)
- Timeout: 5.0s

#### `set_volume`
```json
{
  "name": "set_volume",
  "payload": {
    "volume": 0.8   // required: float in [0.0, 1.0]; 1.0 = 100%, 0.0 = silence
  }
}
```
- Timeout: 5.0s

#### `set_mute`
```json
{
  "name": "set_mute",
  "payload": {
    "muted": true   // required: bool
  }
}
```
- Timeout: 5.0s

#### `resize`
```json
{
  "name": "resize",
  "payload": {
    "width": 1920,   // required: positive int
    "height": 1080   // required: positive int
  }
}
```
- Timeout: 5.0s

#### `set_fullscreen`
```json
{
  "name": "set_fullscreen",
  "payload": {
    "active": true   // required: bool
  }
}
```
- Timeout: 5.0s

#### `set_surface_size`
```json
{
  "name": "set_surface_size",
  "payload": {
    "width": 1920,       // required: positive int
    "height": 1080,      // required: positive int
    "fullscreen": false  // optional: bool
  }
}
```
- Timeout: 5.0s

#### `set_tracks`
```json
{
  "name": "set_tracks",
  "payload": {
    "audio_id": "1",       // required: string (empty = no change)
    "sub_id": "2",         // required: string (empty = no change)
    "sub_visibility": true // required: bool
  }
}
```
- Timeout: 5.0s

#### `set_sub_delay`
```json
{ "name": "set_sub_delay", "payload": { ... } }
```
- Timeout: 5.0s (payload validator not shown in source but command is registered)

#### `load_external_sub`
```json
{ "name": "load_external_sub", "payload": { ... } }
```
- Timeout: 5.0s

#### `set_filters`
```json
{ "name": "set_filters", "payload": { ... } }
```
- Timeout: 5.0s (payload structure not fully validated in source; filter string presumably in payload)

#### `set_tone_mapping`
```json
{ "name": "set_tone_mapping", "payload": { ... } }
```
- Timeout: 5.0s

#### `set_icc_profile`
```json
{ "name": "set_icc_profile", "payload": { ... } }
```
- Timeout: 5.0s

#### `set_sub_style`
```json
{ "name": "set_sub_style", "payload": { ... } }
```
- Timeout: 5.0s

#### `set_hwaccel`
```json
{ "name": "set_hwaccel", "payload": { ... } }
```
- Timeout: 5.0s

#### `set_color_management`
```json
{ "name": "set_color_management", "payload": { ... } }
```
- Timeout: 5.0s

### All Known Events (sidecar → app) with Payload Fields

#### `ready`
- No payload fields specified (Phase 2 skeleton event)

#### `ack`
- No dedicated payload; carries `seqAck` in envelope to acknowledge a command

#### `heartbeat`
- No payload; session-independent (empty `sessionId`); never considered stale

#### `first_frame`
```json
{
  "name": "first_frame",
  "payload": {
    "width": 1920,       // required: positive int — decoded frame width
    "height": 1080,      // required: positive int — decoded frame height
    "codec": "h264",     // required: non-empty string — codec used
    "ptsSec": 0.0        // optional: float — PTS of first frame
  }
}
```

#### `time_update`
```json
{
  "name": "time_update",
  "payload": {
    "positionSec": 45.123,      // required: finite float >= 0 — current playback position
    "durationSec": 1440.0,      // optional: finite float >= 0 — total duration
    "audioClockSec": 45.120,    // optional: finite float >= 0 — audio master clock position
    "isAudioMaster": true,      // optional: bool — true when audio drives the clock
    "active_audio_id": "1",     // optional: string
    "active_sub_id": "2"        // optional: string
  }
}
```

#### `state_changed`
```json
{
  "name": "state_changed",
  "payload": {
    "state": "playing",          // required: one of VALID_PLAYER_STATES
    "previousState": "paused"    // optional: one of VALID_PLAYER_STATES
  }
}
```
Valid states: `"idle"`, `"opening"`, `"ready"`, `"playing"`, `"paused"`, `"seeking"`, `"buffering"`, `"error"`, `"closed"`

#### `eof`
- No payload fields specified; signals end of file reached

#### `closed`
- No payload fields specified; confirms `stop` command — sidecar process stays alive

#### `error`
```json
{
  "name": "error",
  "payload": {
    "message": "human-readable error",   // checked for open lifecycle
    "code": "SOME_CODE"                  // also checked if message absent
  }
}
```

#### `tracks_changed`
```json
{
  "name": "tracks_changed",
  "payload": {
    "audio": [
      { "id": "1", "lang": "ja", "title": "Japanese" },   // id and lang required; title optional
      { "id": "2", "lang": "en" }
    ],
    "subtitle": [
      { "id": "3", "lang": "en", "title": "English SDH" }
    ],
    "active_audio_id": "1",    // required: string (empty = none active)
    "active_sub_id": "3"       // required: string (empty = none active)
  }
}
```
Note: track entry field names in `tracks_changed` use `"id"`, `"lang"`, `"title"` (strings).
Compare with TrackPopover's `populate()` which uses `"id"`, `"lang"`, `"title"`, `"codec"`, `"type"` — codec and type are extra fields for display only.

#### `decode_error`
```json
{
  "name": "decode_error",
  "payload": {
    "code": "UNSUPPORTED_CODEC",          // required: one of DECODE_ERROR_CODES
    "message": "H.265 not supported",     // required: non-empty string
    "path": "/path/to/file.mkv",          // optional: string
    "codec": "hevc"                        // optional: string
  }
}
```
Decode error codes:
- `"UNSUPPORTED_CODEC"` — not retriable
- `"UNSUPPORTED_CONTAINER"` — not retriable
- `"DECODE_INIT_FAILED"` — not retriable
- `"DECODE_FAILED"` — retriable
- `"OPEN_FAILED"` — not retriable
- `"FIRST_FRAME_TIMEOUT"` — retriable

#### `audio_error`
```json
{
  "name": "audio_error",
  "payload": {
    "code": "AUDIO_DEVICE_STARTUP_FAILED",  // required: one of AUDIO_ERROR_CODES
    "message": "Could not open audio device",  // required: non-empty string
    "deviceName": "Speakers",               // optional: string
    "detail": "WASAPI error -2147219396"    // optional: string
  }
}
```
Audio error codes:
- `"AUDIO_DEVICE_STARTUP_FAILED"` — not recoverable
- `"AUDIO_SYNC_FAILED"` — recoverable (fallback to video-only clock)
- `"AUDIO_DEVICE_LOST"` — not recoverable

#### `canvas_error`
```json
{
  "name": "canvas_error",
  "payload": {
    "code": "RENDER_BRIDGE_ATTACH_FAILED",  // required: one of CANVAS_ERROR_CODES
    "message": "Failed to attach render bridge",  // required: non-empty string
    "detail": "OS error detail"             // optional: string
  }
}
```
Canvas error codes:
- `"RENDER_BRIDGE_ATTACH_FAILED"`
- `"RENDER_BRIDGE_DETACHED"`
- Always a hard failure — host must not stay in playing/ready state

#### `fullscreen_changed`
```json
{
  "name": "fullscreen_changed",
  "payload": {
    "active": true,   // required: bool — true = now fullscreen
    "width": 1920,    // optional: positive int — surface width after transition
    "height": 1080    // optional: positive int
  }
}
```

#### `sub_visibility_changed`
```json
{
  "name": "sub_visibility_changed",
  "payload": {
    "visible": true   // required: bool
  }
}
```

#### `subtitle_text`
- Payload structure not fully defined in source (listed in ALL_EVENTS)

#### `sub_delay_changed`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `external_sub_loaded`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `filters_changed`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `media_info`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `tone_mapping_changed`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `icc_profile_changed`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `hwaccel_status`
- Payload structure not defined in source (listed in ALL_EVENTS)

#### `d3d11_texture`
- "Holy Grail" D3D11 shared texture for zero-copy GPU presentation (listed in ALL_EVENTS, no payload definition in source)

#### Shared-memory frame surface events (Phase 4)

##### `shm_offer`
```json
{
  "name": "shm_offer",
  "payload": {
    "shmName": "tankoban_shm_001",  // required: non-empty string
    "slotCount": 4,                  // required: positive int — number of frame slots in ring
    "slotBytes": 8294400,            // required: positive int — bytes per slot
    "width": 1920,                   // required: positive int
    "height": 1080,                  // required: positive int
    "pixelFormat": "bgra8"           // required: must be "bgra8" (only valid format)
  }
}
```

##### `resize_ack`
```json
{
  "name": "resize_ack",
  "payload": {
    "width": 1920,    // required: positive int — applied width
    "height": 1080    // required: positive int — applied height
  }
}
```

##### `shm_ready`
```json
{
  "name": "shm_ready",
  "payload": {
    "shmName": "tankoban_shm_001",  // required: non-empty string
    "slotCount": 4,                  // required: positive int
    "slotBytes": 8294400             // required: positive int
  }
}
```

---

## 4. transport.py

### Sending Messages (app → sidecar stdin)
- `CommandTransport.send(name, payload)` builds command via `make_command()`, registers in `PendingCommands`, then calls `_write_raw()`
- `_write_raw(cmd)`: calls `encode_command(cmd)` → JSON bytes + `\n`, then calls `write_fn(bytes)` under `_write_lock`
- `write_fn` is the sidecar stdin write callable; set via `set_write_fn(fn)`
- Write errors (`BrokenPipeError`, `OSError`) are logged but not propagated — caller does not raise
- Thread-safe: `_write_lock` (threading.Lock) guards all writes

### Reading Messages (sidecar stdout → app)
- Reading is **not** in transport.py — transport only handles the outbound queue and ack matching
- Reading is done upstream (in the process manager, not shown in these files)
- Parsed events arrive via `transport.receive_event(evt: dict)` calls from the reader thread

### Ack Lifecycle
- Every sent command is registered in `PendingCommands` with a per-command timeout deadline
- When an event with `seqAck` arrives, `receive_ack(seq_ack)` removes the pending entry
- Duplicate acks (same seq received twice) are logged and ignored
- `SeqAckMonitor` tracks out-of-order acks for diagnostics (does not reject them)

### Timeout Monitor Thread
- `start_monitor()` spawns `threading.Thread(target=_timeout_monitor_loop, daemon=True)`
- Thread name: `"cmd-timeout-monitor"`
- Poll interval: `TIMEOUT_CHECK_INTERVAL_SEC = 0.5` seconds (injectable for tests)
- Each tick: `PendingCommands.check_timeouts()` → list of expired (seq, name, session_id) → fires `on_timeout` callback
- Each tick also calls `on_lifecycle_check` callback (used by client to check first-frame deadline)
- `stop()`: sets stop event, joins thread (timeout=2.0s), clears pending commands

### Error Handling on Pipe Close
- `BrokenPipeError` or `OSError` on write: logged as error, command is still in pending (will time out)
- No automatic retry or reconnect in transport layer
- No stdout error handling in transport (that's the process manager's responsibility)

### Threading Model
- **Write thread**: any thread (protected by `_write_lock`) — commands can be sent from Qt main thread
- **Monitor thread**: dedicated daemon thread `"cmd-timeout-monitor"` — calls timeout and lifecycle callbacks
- **Event dispatch thread**: not in transport — caller's reader thread calls `receive_event(evt)`

---

## 5. session.py

### What SessionManager Manages
- Single `_session_id` string: the active playback session identifier
- `_generation` int: monotonically increasing per `new_session()` call
- `_stopped` bool: True after `clear()`, prevents stale events from reviving a dead session
- Thread-safe via `threading.Lock`

### Session ID Format
- Generated: `"{prefix}_{timestamp_ms}_{generation}"` where timestamp is `int(time.time() * 1000)`
- Example: `"s_1742900000000_3"`
- Default prefix: `"s"`

### Stale Event Filtering
- `is_stale(event_session_id)`:
  - If `_stopped`: all session-bound events are stale (even matching the previous session ID)
  - If `event_session_id` is empty `""`: **never stale** (session-independent async events like heartbeat)
  - Otherwise: stale if `event_session_id != _session_id`
- This means heartbeat/async events always pass through

### open → first_frame → play Sequence
Not directly managed in session.py — session only tracks identity.
The sequence is coordinated by `OpenLifecycle` in client.py (see section 6).

### EOF and Auto-advance
Not in session.py — session is purely identity management.

### Session Lifecycle
1. `new_session(prefix)` → generates new ID, clears `_stopped`, returns ID
2. `set_current(session_id)` → forces a caller-supplied ID (for deterministic testing)
3. `clear()` → sets `_session_id = ""`, `_stopped = True`
4. After `clear()`, all session-bound events rejected until next `new_session()`

---

## 6. client.py

### Public API (`SidecarClient`)

#### Lifecycle
- `attach(write_fn)` — connect stdin write callable after sidecar spawns
- `detach()` — disconnect (set write_fn to None)
- `start()` — begin timeout monitoring (calls `transport.start_monitor()`)
- `stop()` — stop monitoring, reset open lifecycle, clear session
- `begin_session(prefix, session_id)` → str — start new playback session; returns session_id
- `end_session()` — stop transport + clear session; call after receiving `closed` event

#### Commands
- `send(name, payload)` → int (seq) — generic send
- `send_open(payload)` → int — send `open` command AND begin `OpenLifecycle` tracking
- `send_ping()` → int
- `send_shutdown()` → int
- `send_resume()` → int — `"resume"` command, no payload
- `send_pause()` → int — `"pause"` command, no payload
- `send_stop()` → int — `"stop"` command, no payload
- `send_seek(position_sec: float)` → int — payload: `{"positionSec": float}`
- `send_set_rate(rate: float)` → int — payload: `{"rate": float}`
- `send_set_volume(level: float)` → int — payload: `{"volume": float}`; 1.0=100%, 0.0=silence
- `send_mute(muted: bool)` → int — payload: `{"muted": bool}`; sends command `"set_mute"`
- `send_resize(width, height)` → int — payload: `{"width": int, "height": int}`
- `send_fullscreen(active: bool)` → int — sends `"set_fullscreen"`, payload: `{"active": bool}`

#### Event Dispatch
- `dispatch_event(evt: dict)` → bool — call from reader thread for each parsed event line
  - Returns False if stale/invalid; returns True if valid and dispatched
  - Internally advances open lifecycle, routes to typed callbacks

#### Callbacks (constructor kwargs)
- `on_timeout(seq: int, cmd_name: str, session_id: str)` — command timed out
- `on_ack(seq: int, cmd_name: str)` — command acked
- `on_valid_event(evt: dict)` — raw event dict for every valid event
- `on_first_frame(session_id: str)` — first frame received after open
- `on_open_error(session_id: str, detail: str)` — open failed or first-frame timed out
- `on_time_update(session_id: str, pos_sec: float, dur_sec: float | None)` — from `time_update` event
- `on_state_changed(session_id: str, state: str)` — from `state_changed` event
- `on_eof(session_id: str)` — from `eof` event
- `on_closed(session_id: str)` — from `closed` event

### Open Lifecycle (`OpenLifecycle`)
States: `IDLE → OPENING → ACKED → FIRST_FRAME` (success) or `ERROR` / `TIMED_OUT` (failure)

Sequence:
1. `client.begin_session()` — generate session ID
2. `client.send_open(payload)` — sends `open` command, calls `lifecycle.begin(seq, session_id)`; state → OPENING
3. Any event with matching `seqAck` arrives → `lifecycle.on_ack()` → state → ACKED
4. `first_frame` event arrives → `lifecycle.on_first_frame()` → state → FIRST_FRAME → `on_first_frame` callback fires
5. OR: `error` event during OPENING/ACKED → state → ERROR → `on_open_error` fires
6. OR: first-frame deadline expires (checked every 0.5s by monitor thread, deadline = FIRST_FRAME_TIMEOUT_SEC = 15s from open send time) → state → TIMED_OUT → `on_open_error` fires

First-frame timeout detail:
- Deadline is set at `time.monotonic() + first_frame_timeout` when `begin()` is called
- `FIRST_FRAME_TIMEOUT_SEC = OPEN_CMD_TIMEOUT_SEC = 15.0s` (same constant)
- `FIRST_FRAME_DEADLINE_SEC = 10.0s` is defined but is a separate doc constant (lifecycle uses OPEN_CMD_TIMEOUT_SEC)
- The transport monitor calls `_check_lifecycle_deadlines()` every 0.5s

### Track Lists Surfaced to UI
- `tracks_changed` event arrives via `dispatch_event()` → routed to `on_valid_event` callback
- No dedicated track callback in `SidecarClient` — caller processes via `on_valid_event`
- `TrackPopover.populate()` accepts the raw track list dicts; caller extracts from event payload

### Threading Notes
- All callbacks fired from the **monitor thread** (timeout/lifecycle deadline) or from **reader thread** (event callbacks)
- Qt callers must use `QMetaObject::invokeMethod` or queued connections to marshal back to main thread
- `wait_for_first_frame(timeout)` — blocks caller thread until first_frame or timeout using `threading.Event`

---

## Summary: C++ Implementation Notes

### Sidecar Protocol (for SidecarProcess.cpp)
1. Write commands as `json + "\n"` to sidecar stdin, UTF-8
2. Read events line-by-line from sidecar stdout, UTF-8
3. Every command: `{"type":"cmd","seq":N,"sessionId":"...","name":"...","payload":{...}}`
4. Every event: `{"type":"evt","name":"...","sessionId":"...","seqAck":N,"payload":{...}}`
5. seqAck in events: match to pending commands to confirm delivery
6. Session IDs: generate on each open, reject events with wrong sessionId
7. Heartbeat events have empty sessionId — never reject them

### FilterPopover (for future C++ widget)
- QFrame, 220–320px wide, `border-radius: 8px`, anchored above chip with 8px gap
- Controls: deinterlace checkbox, brightness/contrast/saturation sliders, normalization checkbox
- All changes debounced 300ms before emitting filter specs
- Active count = non-default controls (0 = no N shown)
- Video filter string: `"yadif=mode=0"` + `"eq=brightness=X:contrast=Y:saturation=Z"` comma-joined
- Audio filter string: `"loudnorm=I=-16"` or `""`
- Dismiss: click outside, re-click chip. No Escape handler. No animation.

### TrackPopover (for future C++ widget)
- QFrame, 200–320px wide, anchored same as filter popover
- Audio tracks: QListWidget, max 4 rows visible (30px each), click selects + dismisses
- Subtitle tracks: same but "Off" item (tid=0) prepended
- Sub delay: ±100ms per click, 0 = reset signal
- Sub style: font size 16–48, margin 0–100, outline bool, all debounced 300ms
- Dismiss: click outside, re-click chip. No animation.

ASSISTANT 2 COMPLETE
