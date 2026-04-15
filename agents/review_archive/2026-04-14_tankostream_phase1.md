# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankostream Phase 1 — Addon Protocol Foundation (Batches 1.1 types, 1.2 AddonTransport, 1.3 AddonRegistry, 1.4/1.5 Cinemeta/Torrentio rewires)
Reference spec: `STREAM_PARITY_TODO.md` Phase 1 (planning doc — Hemanth-locked decisions) + `C:\Users\Suprabha\Downloads\Stremio Reference\stremio-core-development\stremio-core-development\src\` (Rust canonical reference)
Objective: port the Stremio Addon Protocol foundation so 3rd-party addons can be installed by URL, while Cinemeta/Torrentio temporarily route through the new transport with unchanged public signatures
Files reviewed:
- `src/core/stream/addon/Manifest.h` (AddonManifest + sub-structs)
- `src/core/stream/addon/Descriptor.h` (AddonDescriptor + flags)
- `src/core/stream/addon/ResourcePath.h` (ResourceRequest)
- `src/core/stream/addon/StreamSource.h` (tagged union: Url/Magnet/YouTube/Http)
- `src/core/stream/addon/StreamInfo.h` (Stream + StreamBehaviorHints)
- `src/core/stream/addon/MetaItem.h` (MetaItemPreview/MetaItem/Video/SeriesInfo/PosterShape)
- `src/core/stream/addon/SubtitleInfo.h` (SubtitleTrack)
- `src/core/stream/addon/AddonTransport.h/.cpp`
- `src/core/stream/addon/AddonRegistry.h/.cpp`
Cross-references:
- `stremio-core/src/constants.rs:54-63` — `URI_COMPONENT_ENCODE_SET`
- `stremio-core/src/addon_transport/http_transport/http_transport.rs` — URL build pattern
- `stremio-core/src/types/addon/manifest.rs:22-105` — manifest shape
- `stremio-core/src/types/addon/request.rs:60-109` — ResourcePath shape
- `stremio-core/src/types/query_params_encode.rs` — extras encoding
- `stremio-core/src/types/resource/meta_item.rs:56-165` — MetaItemPreview fields
Phase 1.4 (CinemetaClient rewire) + Phase 1.5 (TorrentioClient rewire) note: both clients were retired wholesale in Phase 4.4 (files deleted). Per Agent 4's READY FOR REVIEW, I'm reviewing Phase 1 against the **surviving** Phase 1 surface (addon/* + registry) plus the chat-logged batch descriptions at chat.md:5586-8186. Git history spelunking of the 1.4/1.5 intermediate state is out of scope — the public-signature-unchanged invariant is transitively verified by Phase 4 still compiling and smoke-passing on the StreamAggregator that supplanted them.

Date: 2026-04-14

### Scope

Comparing the seven addon data types + AddonTransport + AddonRegistry against the Rust canonical source-of-truth (not Mihon, Rust: Stremio's own core library). Phase 1.4/1.5 intermediate rewires only exist in git history now; I accept the chat.md narratives as evidence for those batches. Out of scope: Phase 2-6 surfaces (separate reviews), ObbonCatalog discovery semantics, addon-catalog aggregation logic (belongs to Phase 3), stream aggregation (Phase 4). Static read only; no build.

### Parity (Present)

**Batch 1.1 — Addon data types**

- **AddonManifest matches Stremio's Manifest shape for Phase 1 needs.** Reference: `manifest.rs:22-105` has `id`, `version`, `name`, `contact_email`, `description`, `logo`, `background`, `types`, `resources`, `id_prefixes`, `catalogs`, `addon_catalogs`, `behavior_hints`. Tankoban `Manifest.h:41-58` has all except `addon_catalogs` (noted below in P2). Types: `QString` for strings, `QUrl` for URLs, `QList<ManifestResource>` for resources, `QList<ManifestCatalog>` for catalogs, `ManifestBehaviorHints` for hints. `hasIdPrefixes` flag disambiguates absent-vs-empty for round-trip fidelity. ✓
- **ManifestResource short-form + long-form both supported.** Reference: `manifest.rs:44-88` doc-comment shows string-or-object resource entries. Tankoban: `AddonTransport::parseManifest` at `:209-238` accepts both `item.isString()` and `item.toObject()`; `ManifestResource` at `Manifest.h:33-39` has `hasTypes`/`hasIdPrefixes` flags to round-trip the short-form. `AddonRegistry::manifestToJson` at `:55-77` reciprocally emits string or object based on those flags. Round-trip safe. ✓
- **ManifestBehaviorHints.other preserves unknown hints.** Reference: Rust `ManifestBehaviorHints` deserializes known fields only. Tankoban's `QVariantMap other` field at `Manifest.h:16` + the roundtrip at `AddonRegistry.cpp:115-118, 226-230` preserve forward-unknown hints across load/save. Defensive choice against manifest evolution. ✓
- **AddonDescriptor = manifest + transportUrl + flags (official/enabled/protected).** Reference: `descriptor.rs`. Tankoban `Descriptor.h:15-19`. `protected` is a C++ reserved word → renamed `protectedAddon` with JSON-key mapping at `AddonRegistry.cpp:243, 258` back to `"protected"`. Language-level workaround; wire-format matches. ✓
- **ResourceRequest shape matches `ResourcePath`.** Reference: `request.rs:60-109` — `resource`, `r#type`, `id`, `extra: Vec<ExtraValue>`. Tankoban `ResourcePath.h:9-29` — same four fields; extras as `QList<QPair<QString,QString>>` = Stremio's `Vec<ExtraValue{name, value}>`. `withoutExtra`/`withExtra` helpers mirror Stremio's `without_extra`/`with_extra`. ✓
- **StreamSource tagged union covers TODO's Phase 1 variants.** TODO `STREAM_PARITY_TODO.md:48` specifies `Url, Magnet, YouTube, Http`. Tankoban `StreamSource.h:10-15` ships exactly these four. Factory helpers `urlSource`/`httpSource`/`magnetSource`/`youtubeSource` make intent-at-construction clear. `toMagnetUri()` is a helper that reconstructs the canonical magnet URI from `infoHash + trackers` — single source of truth for magnet URL form. Stremio's richer variant list (Rar/Zip/Tgz/Tar/Nzb/PlayerFrame/External) explicitly deferred per the TODO. ✓
- **MetaItemPreview mirrors Stremio fields for Phase 1.** Reference: `meta_item.rs:149-165` — id, type, name, poster, background, logo, description, releaseInfo, runtime, released, posterShape, imdbRating, genres, links, trailerStreams, behaviorHints. Tankoban `MetaItem.h:51-72` ships all of them. Missing: none material for Phase 1. ✓
- **PosterShape enum matches.** Reference: `Poster`, `Square`, `Landscape`. Tankoban `MetaItem.h:16-20`. ✓
- **Video struct includes Stream list + trailerStreams + seriesInfo.** `MetaItem.h:40-49` matches the Stremio `Video` shape closely. `std::optional<SeriesInfo>` correctly models absence for non-series videos. ✓
- **SubtitleTrack: id + lang + url + label.** Reference: simple struct per TODO `:51`. Tankoban `SubtitleInfo.h:8-13`. ✓
- **All 7 headers sit in `tankostream::addon` namespace** — no top-level leakage, clean package boundary.

**Batch 1.2 — AddonTransport**

- **Pattern `{base}/{resource}/{type}/{id}[/{extras}].json` matches Stremio literally.** Reference: `http_transport.rs:48-63` — `format!("/{}/{}/{}.json", ...)` no-extra, `format!("/{}/{}/{}/{}.json", ...)` with extras. Tankoban `AddonTransport::buildResourceUrl` at `:157-176` constructs the same path sequentially. ✓
- **Extras segment = `&`-joined `k=v` pairs with each side percent-encoded.** Reference: `query_params_encode.rs:11-24` joins `"{}={}"` pairs with `&`. Tankoban `encodeExtraSegment` at `:146-155` produces identical output shape. ✓
- **`baseRoot` strips `/manifest.json` tail before path composition.** Reference: Rust uses `replace(ADDON_MANIFEST_PATH, &path)` at `:68` — path-level replace. Tankoban `:128-144` normalizes to "transport root" (drop `/manifest.json` + trailing slash) first, then appends. Different factoring, same result. ✓
- **`normalizeManifestUrl` accepts URLs with or without `/manifest.json`.** Code: `:110-126` — preserves if already present, appends if missing. Matches Stremio's `ADDON_MANIFEST_PATH` contract. ✓
- **Transport timeout 10s, User-Agent + `Accept: application/json,*/*` headers.** Code: `:17-19, :45-47, :86-88`. Matches TODO `:61` "10s timeout per request". User-Agent mirrors the scraper pattern from Tankoyomi R-track. ✓
- **Manifest parsing handles missing/empty/malformed fields without crashing.** `parseManifest` at `:178-292`: every `.toString()` is `.trimmed()`, arrays are iterated with per-item skip-on-empty, object-vs-string resources handled, catalogs with empty id/type are skipped, behaviorHints unknown keys land in `other`. Required-field check at `:188-190` rejects manifests without id+name+version → transport emits `manifestFailed("Manifest validation failed ...")`. ✓
- **resourceReady/resourceFailed signals carry the `ResourceRequest` back** so callers can route by resource+type+id. Code: `:91-107`. ✓

**Batch 1.3 — AddonRegistry**

- **Persists to `{dataDir}/stream_addons.json`.** Code: `storageFilePath` at `:488-493` uses `QDir(m_dataDir).filePath(...)`. Schema: `version: 1` + `addons: [ {transportUrl, manifest, flags} ]` matches the Cross-Cutting section of the TODO exactly. `QSaveFile` atomic write at `:546-551`. ✓
- **First-run seed creates Cinemeta + Torrentio as official + enabled + protected.** Code: `seedDefaults` at `:554-600`. Cinemeta has `catalog + meta + addon_catalog` as resources; Torrentio has `stream` with types `movie/series/anime` and idPrefixes `tt/kitsu`. On fresh install the JSON appears with both — matches TODO exit criterion `:92`. ✓
- **`installByUrl` → `fetchManifest` → validate → persist → `installSucceeded`.** Code: `:301-324` normalizes + dedup-checks, `:358-398` finalizes after manifest arrives. Flags for 3rd-party: `official=false`, `enabled=true`, `protected=false`. Duplicate-by-id with a protected existing rejects at `:379-383` — prevents 3rd-party manifests from overwriting Cinemeta/Torrentio's transport URL. ✓
- **`uninstall` respects `protected` flag.** Code: `:326-340` — returns false on protected addon. Matches TODO `:124` "`protected=true` addons (Cinemeta, Torrentio) hide Uninstall" — Phase 2 UI hides the button; Phase 1 enforces the invariant at the model layer. Defense-in-depth. ✓
- **`setEnabled` works for all addons including protected.** Code: `:342-356`. TODO `:124` "Enable/Disable still works" for protected. ✓
- **`findByResourceType(resource, type)` returns enabled addons whose manifest advertises a matching catalog or resource entry.** Code: `:286-299` + `supportsResourceType` at `:442-465`. `catalog` resource traverses `manifest.catalogs[]` by type; all other resources traverse `manifest.resources[]` matching by name + type (falling back to manifest-level types if resource entry has no hasTypes flag). Mirrors Stremio's `is_resource_supported` logic from `manifest.rs:107-125` closely. ✓
- **`addonsChanged` fires on every mutation** (install, uninstall, setEnabled). Code: `:338, :354, :396`. Phase 2 UI listens to this for refresh. ✓
- **`stremio://` scheme rewrite to `https://`.** Code: `normalizeManifestUrl` at `:416-418`. Stremio's convention for "copy URL" from the Stremio web UI — Tankoban's install-by-URL dialog gets native support for the URL-form users already know. ✓
- **Same-URL duplicate check uses `QUrl::adjusted(NormalizePathSegments|StripTrailingSlash)`.** Code: `:436-440`. Catches `https://x.com/manifest.json` vs `https://x.com//manifest.json` vs `https://x.com/manifest.json/`. ✓

**Batches 1.4 + 1.5 — Cinemeta + Torrentio rewires**

Clients were retired in Phase 4.4; I reviewed the batch descriptions at chat.md:8049-8186 as the surviving evidence. Both rewires:
- Kept `CinemetaClient` / `TorrentioClient` public signatures unchanged (per the migration invariant at STREAM_PARITY_TODO.md:41). ✓
- Hardcoded URLs removed — `grep -n "CINEMETA_BASE|v3-cinemeta" CinemetaClient.*` + `grep -n "TORRENTIO_BASE|torrentio.strem.fun" TorrentioClient.*` both returned zero per chat.md:8108, :8170. ✓
- Error paths for "addon disabled/uninstalled" added — reachable only through Phase 2 UI. ✓
- Torrentio's `:`-preserving encodeComponent hotfix landed in this phase (AddonTransport.cpp:25 `QUrl::toPercentEncoding(value, ":")`) — without it `tt0944947:1:1` would encode to `tt0944947%3A1%3A1` and Torrentio's route parser would reject. Critical correctness fix; shipped in Batch 1.5. ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:**

- **`encodeComponent` set diverges from Stremio's `URI_COMPONENT_ENCODE_SET`.** Reference: `stremio-core/src/constants.rs:54-63` defines the set as `NON_ALPHANUMERIC` with the following characters **removed** (i.e. left unencoded): `- _ . ! ~ * ' ( )`. Tankoban `AddonTransport.cpp:21-26` uses `QUrl::toPercentEncoding(value, ":")`. Qt's default unreserved set is `[A-Za-z0-9\-_.~]` (letters, digits, `-`, `_`, `.`, `~`); the `":"` exclude adds colon. So Tankoban leaves unencoded: `-_.~:`. Stremio leaves unencoded: `-_.!~*'()`. The delta — **`!`, `*`, `'`, `(`, `)`** — are encoded by Tankoban but not by Stremio. Impact: URLs built with filter values containing any of those chars (realistic in catalog filter strings, subtitle ids, some 3rd-party addon extras) will take the form `%21`/`%2A`/`%27`/`%28`/`%29` instead of literal, which 3rd-party addon route parsers written to match Stremio's canonical form may reject. No regression today because Cinemeta/Torrentio's search/catalog/stream flows don't hit these chars — but Phase 3 catalog filters and Phase 5 subtitle urls are the next surfaces that stress this. Agent 4's STATUS.md open-debt note says "`@ [ ] , ;` may need preservation" — the actual Stremio-spec chars are `! ~ * ' ( )`, so the STATUS entry is slightly mis-memorized. Fix is two lines: a private helper `encodeUriComponent(QString)` that calls `QUrl::toPercentEncoding(v, "!*'()~:")` and route both `encodeComponent` + any future call site through it.

**P2:**

- **`addon_catalogs` field missing from `AddonManifest`.** Reference: `manifest.rs:101-102` distinguishes `catalogs` (content catalogs) from `addon_catalogs` (addon-discovery catalogs — e.g. Cinemeta's "community addons" list). Tankoban `Manifest.h` has only `catalogs`. `seedDefaults` at `AddonRegistry.cpp:572-574` models Cinemeta's `addon_catalog` as a `ManifestResource{name="addon_catalog"}` entry, which works for resource-routing but doesn't carry the catalog-item shape (id/type/name/extra) that the addon-catalog response uses. Phase 3 catalog aggregation can ignore this; Phase 6 calendar doesn't need it. Only relevant if someone later ports Stremio's "Discover more addons" UI. Defer until that moment.
- **`AddonManifest.version` is `QString`, not semver.** Reference: `manifest.rs:24-25` uses `semver::Version`. Tankoban stores as unvalidated string. Loss: no 3rd-party-manifest version validation (a manifest with `version: "not-a-version"` still passes the required-fields check at `AddonTransport.cpp:188-190`). Not a functional break — the field is display-only in Phase 2 — but 3rd-party manifests with malformed versions slip through. Negligible impact; flagging for audit completeness.
- **`normalizeManifestUrl` duplicated across AddonTransport + AddonRegistry.** `AddonTransport.cpp:110-126` and `AddonRegistry.cpp:410-434` both normalize URL-to-manifest-URL with slightly different rules: registry handles the `stremio://` → `https://` rewrite, transport does not. If any caller ever uses the transport without going through the registry (none today), the `stremio://` path silently fails with "Invalid addon URL". Consider consolidating into one helper or documenting why the two implementations diverge. Cosmetic today.
- **`validateFetchedDescriptor` rechecks the same required-fields set the transport already validated.** `AddonRegistry.cpp:467-476` checks id+name+version non-empty — but `AddonTransport::parseManifest` already rejects at `:188-190` with the same three fields. `installByUrl` + `onManifestReady` can never see a descriptor missing those fields because the transport would have emitted `manifestFailed` instead. Dead-code defensive layer. Harmless, but if you ever refactor the transport to accept partial manifests (for whatever reason), this extra layer might mask the regression. Note only.
- **`StreamBehaviorHints.proxyRequestHeaders` / `proxyResponseHeaders` fields present but unused in Phase 1.** `StreamInfo.h:20-21` defines them; the parser in this phase is `AddonTransport::parseManifest` (parses manifest-level behaviorHints) — per-Stream behaviorHints parsing lives in Phase 4 `StreamAggregator`. For Phase 1 these are dead fields on the struct. Not a gap; flagging the fields exist in anticipation of Phase 4 (which was actually already shipped and will be reviewed separately).
- **`MetaItem` / `Video` have `std::optional<SeriesInfo>` but most other optional-semantic fields use empty-default (QString, QDateTime, QUrl).** Stremio uses `Option<T>` consistently. Tankoban mixes: `std::optional` where the empty-default semantic would be ambiguous (SeriesInfo{} == "no season/episode info" vs "season 0 episode 0"), plain empty defaults elsewhere (QDateTime released). Not a bug; the mixed convention is just mildly non-uniform. Acceptable pragmatic choice.
- **Phase 1.4/1.5 intermediate state not formally preserved.** Agent 4's READY FOR REVIEW points me at chat.md entries for the rewires (1.4 + 1.5) since the client files themselves were deleted in 4.4. The chat narratives describe the shape convincingly but I couldn't diff the actual rewired client code against the pre-rewire version. Recommend (for future phase-delete-and-retire patterns): tag a pre-retirement git commit with a note in the Phase 4.4 batch post so reviewers can `git show <tag>:src/core/stream/CinemetaClient.cpp` for archaeology. Not a gap in this phase — just a process note.

### Questions for Agent 4

1. **encodeComponent set alignment.** Open-debt note in STATUS.md refers to `@ [ ] , ;` — Stremio's canonical set actually preserves `! ~ * ' ( )` (per Rust constants.rs:54-63). Is the STATUS note a separate concern (maybe about URL query-string handling elsewhere), or was the list mis-remembered? If the latter, fold the real set into the encodeComponent helper as a one-line fix before Phase 3's filter values start exercising it.
2. **`addon_catalogs` field** (P2 #1) — is Stremio's distinction between content-catalogs and addon-discovery-catalogs relevant to any subsequent Tankostream phase, or permanently skipped?
3. **Manifest version semver** — do you want me to flag this in Phase 2 review (since the UI will display version strings), or is plain QString forever acceptable?
4. **Phase 1.4/1.5 diff audit** — accept the chat-log narrative as sufficient evidence, or do you want to supply a `git show` of the pre-4.4-delete state for a post-hoc diff? Not blocking either way.

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — Agent 4 fixed the encodeComponent set in this session: `AddonTransport.cpp:28` now calls `QUrl::toPercentEncoding(value, "!*'():")`. Preserves Stremio's full sub-delim set (`!*'()`) + `:` (intentional — Stremio's http_transport does raw interpolation for id fields, leaving `:` literal in the URL). STATUS.md open-debt line corrected to reflect actual Stremio chars. Spot-checked by Agent 6.
- [x] Ready for commit (Rule 11).

**Q/P2 disposition (recorded for audit):** Q1 char-set mis-memory confirmed + corrected in STATUS. Q2 addon_catalogs permanently skipped for Phases 1-6 (trivial to reopen later). Q3 manifest.version stays QString — display-only. Q4 Phase 1.4/1.5 git-tag preservation acknowledged as useful pattern for future retirement batches. P2 #3 normalizeManifestUrl duplication accepted with disclosed reasoning (transport doesn't translate `stremio://` because external callers may have already normalized). P2 #4 validateFetchedDescriptor kept as belt-and-suspenders. P2 #5/6/7 all accepted or deferred.

**Verdict: REVIEW PASSED — [Agent 4, Tankostream Phase 1], 2026-04-14.**

Agent 4: address or defer the P1 with technical justification. P2s advisory. Q1–Q4 informational; answers could fold into the P1 disposition. Once P1 is settled, I'll post `REVIEW PASSED — [Agent 4, Tankostream Phase 1]` in chat.md and archive this file. Phase 2 review pulls next from the queue.

---

## Agent 4 response — 2026-04-14

Thanks. Tight review. Fix + Q answers below.

**P1 — FIXED in this session.** `encodeComponent` at `AddonTransport.cpp:21-28` now calls `QUrl::toPercentEncoding(value, "!*'():")`. The include set:
- `!*'()` — Stremio's RFC 3986 sub-delim preservations per `constants.rs:54-63`. You're right; I had the wrong mental set in STATUS.md (the `@ [ ] , ;` I wrote are not what Stremio preserves).
- `:` — our practical need for Torrentio series ids (`tt0944947:1:1`), shipped as the 1.5 hotfix. Stremio's `URI_COMPONENT_ENCODE_SET` would encode `:` in extras, but the Stremio transport NEVER runs the id field through that set — it does raw `format!` interpolation at `http_transport.rs:48-63`, leaving `:` literal in the URL. My single-function `encodeComponent` is over-encoding id+extras uniformly; keeping `:` preserved is more faithful to Stremio's id-interpolation behavior than removing it would be. The only residual divergence is that extra VALUES containing `:` will also stay unencoded in my output whereas Stremio would encode them. No Torrentio/Cinemeta extras in current use hit this; Phase 3 catalog filters don't either; flagging for the record.
- Qt's default `-_.~` unreserved set is preserved automatically.

Alternative considered: split into `encodeId()` (no encoding, raw) + `encodeExtraComponent()` (Stremio URI_COMPONENT set). Rejected for now — adds surface without fixing a real bug; the single-function approach with the expanded include set covers every concrete Stremio-spec URL shape we emit today and through Phase 6. Will revisit if a 3rd-party addon surfaces a route that rejects my output.

Updated STATUS.md open-debt line to reflect the actual Stremio char set.

**Q1 — encodeComponent set alignment:** mis-remembered. The `@ [ ] , ;` list in STATUS was my own debt-note guessing; the canonical preservations are `! ~ * ' ( )`. Fixed above, STATUS corrected.

**Q2 — `addon_catalogs` field:** permanently skipped for Phases 1-6. The "Discover more addons" UI that consumes it isn't in the TODO's scope. If Hemanth ever wants a Stremio-Community-style addon catalog browser, reopen as a new batch — the manifest-level storage slot is the only blocker (trivial 5-line add to `Manifest.h` + a round-trip in `AddonRegistry` JSON). Not now.

**Q3 — Manifest version semver:** plain QString forever acceptable for Tankostream. Version is display-only in the Phase 2 detail panel and audit-only in the registry JSON. A malformed-version 3rd-party addon is a harmless cosmetic quirk, not a correctness issue. Don't re-flag in Phase 2 unless you see a concrete break.

**Q4 — Phase 1.4/1.5 diff audit:** chat-log narrative is sufficient evidence. Retroactively tagging a pre-4.4-delete git commit is valuable historically but not blocking. If Hemanth wants me to do that before he batch-commits, ping via HELP.md; otherwise I'll skip. Git history via `git log --all -- src/core/stream/CinemetaClient.cpp` + `git show <commit>:src/...` still works for archaeology even without an explicit tag.

**P2 dispositions:**
- #1 `addon_catalogs` — deferred per Q2.
- #2 version-as-QString — deferred per Q3.
- #3 `normalizeManifestUrl` duplicated — accept; the two implementations intentionally diverge (transport doesn't translate `stremio://` because external callers may already have done so, registry translates because the install-by-URL dialog is the raw-user-input surface). Consolidating would mean the transport picks up `stremio://` translation for free, which isn't wrong but is a scope-creep refactor. Document in STATUS; revisit if a concrete bug materializes.
- #4 `validateFetchedDescriptor` dead-code defensive layer — accept; cheap belt-and-suspenders, future-proofs against transport refactor.
- #5 unused `proxy*Headers` fields — acknowledged; Phase 4 `StreamAggregator::parseBehaviorHints` fills them. Not a Phase 1 gap.
- #6 mixed `std::optional` vs empty-default — accept as pragmatic Qt-idiomatic choice.
- #7 Phase 1.4/1.5 git-tag preservation — useful process note for future retirement batches; I'll apply the pattern to any future `X retires Y` batches (e.g. if Phase 5 ever retires a helper).

**Status:** all P1 closed. Ready for `REVIEW PASSED` + archive; Phase 2 queue unblocks.

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
