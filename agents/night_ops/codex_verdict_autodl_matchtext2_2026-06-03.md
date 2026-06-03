codex.cmd : Reading prompt from stdin...
At line:16 char:47
+ ... rompt = $head + "`n" + $diff; $prompt | & codex.cmd exec 2>&1 | Out-F ...
+                                             ~~~~~~~~~~~~~~~~~~~~~
    + CategoryInfo          : NotSpecified: (Reading prompt from stdin...:String) [], RemoteException
    + FullyQualifiedErrorId : NativeCommandError
 
OpenAI Codex v0.131.0
--------
workdir: C:\Users\Suprabha\Desktop\Tankoban 2
model: gpt-5.5
provider: openai
approval: never
sandbox: workspace-write [workdir, /tmp, $TMPDIR]
reasoning effort: high
reasoning summaries: none
session id: 019e8cd0-ed8d-7cd2-82d6-1a27024dc0f2
--------
user
Cross-model RE-REVIEW for Tankoban 2 (requested by Agent 4/Opus). Read-only; do NOT edit.

This is the SECOND pass on the auto-download show-identity gate fix (matchText). In your FIRST review you APPROVED the 
runtime behavior in full (One Piece unblocked, wrong-show protection preserved, empty-matchText fallback correct, 
fields null-safe, isCamRip/sourceScore still on title, tests not tautologies) but returned REQUEST-CHANGES for ONE 
governance reason: the patch added emoji literals to code comments and test strings, violating AGENTS.md "No emojis in 
code".

THE ONLY DELTA since your approval: every emoji badge ("[user] 2176 [disk] 1.34 GB [gear] NyaaSi") in my comments and 
test string literals was replaced with an ASCII badge ("[2176 seeders | 1.34 GB | NyaaSi]"). No logic changed. The 
test still encodes the same contract (badge titles lack the tokens one/piece; the filename in matchText carries them). 
All 20 AutoSourcePicker tests pass; full suite 376/377 (the 1 fail is the pre-existing Books test). App rebuilt clean.

VERIFY:
1. Confirm NO emoji literals remain in the diff (comments or test strings).
2. Confirm the ASCII test titles still lack the show tokens (one/piece) so the badge-rejection cases are still 
meaningful, and the matchText still carries the One Piece filename so the pass cases hold.
3. Confirm no logic changed vs the version you already approved.

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason.

================ DIFF UNDER REVIEW ================
diff --git a/src/core/stream/AutoSourcePicker.cpp b/src/core/stream/AutoSourcePicker.cpp
index c20e337..5dc455d 100644
--- a/src/core/stream/AutoSourcePicker.cpp
+++ b/src/core/stream/AutoSourcePicker.cpp
@@ -71,7 +71,12 @@ std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidat
         if (isCamRip(cand.title)) continue;                       // camcorder rip
         // Show-identity gate: never pick a release that isn't the requested
         // show, no matter how well-seeded (the One Piece -> Community bug).
-        if (!showTitle.isEmpty() && !titleMatchesShow(cand.title, showTitle)) continue;
+        // DOWNLOAD BUG 2026-06-03 ? gate on matchText (full raw identity blob)
+        // when present; some addons put a stats badge in `title`, which lacks
+        // the show name and rejected every real result. Falls back to `title`
+        // so callers/tests that only set `title` are unaffected.
+        const QString& idText = cand.matchText.isEmpty() ? cand.title : cand.matchText;
+        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) continue;
         survivors.append(i);
     }
     if (survivors.isEmpty()) return std::nullopt;                 // no source found
diff --git a/src/core/stream/AutoSourcePicker.h b/src/core/stream/AutoSourcePicker.h
index ccc83b3..0138c61 100644
--- a/src/core/stream/AutoSourcePicker.h
+++ b/src/core/stream/AutoSourcePicker.h
@@ -12,6 +12,16 @@ struct SourceCandidate {
     int     seeders = 0;
     qint64  sizeBytes = 0;
     int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown
+
+    // DOWNLOAD BUG 2026-06-03 ? text the show-identity gate matches against.
+    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
+    // anime-shaped Torrentio) put a stats badge ("[2176 seeders | 1.34 GB |
+    // NyaaSi]") there instead of the filename, so gating on `title` rejected every
+    // One Piece result ("No 1080p source found"). matchText carries the full
+    // raw identity blob (name + description + fileNameHint + ?) so the gate can
+    // find the show name wherever the addon stashed it. Empty == fall back to
+    // `title` (preserves every existing caller + test that only sets `title`).
+    QString matchText;
 };
 
 // Silent best-source selection for Theatre's one-tap download.
diff --git a/src/ui/pages/StreamPage.cpp b/src/ui/pages/StreamPage.cpp
index f5a856f..c8644b5 100644
--- a/src/ui/pages/StreamPage.cpp
+++ b/src/ui/pages/StreamPage.cpp
@@ -3359,27 +3359,55 @@ void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>&
     qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                       << "active=" << (m_pendingAuto.active ? "y" : "n");
 
+    // DOWNLOAD BUG 2026-06-03 ? build the show-identity match text from the RAW
+    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
+    // put a stats badge ("[2176 seeders | 1.34 GB | NyaaSi]") in displayTitle,
+    // which lacks the show name and made the gate reject every real One Piece result
+    // ("No 1080p source found"). The raw blob carries the filename wherever the
+    // addon stashed it (name / description / fileNameHint / parsedFilename), so
+    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
+    // title (its job in the picker is unchanged).
+    auto identityBlob = [](const tankostream::stream::StreamPickerChoice& ch) {
+        return QStringList{
+            ch.displayTitle,
+            ch.stream.name,
+            ch.stream.description,
+            ch.stream.source.fileNameHint,
+            ch.fileNameHint,
+            ch.stream.behaviorHints.filename,
+            ch.stream.behaviorHints.other
+                .value(QStringLiteral("parsedFilename")).toString(),
+        }.join(QLatin1Char('\n'));
+    };
+
     QList<tankostream::stream::SourceCandidate> cands;
     cands.reserve(choices.size());
     for (const auto& c : choices) {
         tankostream::stream::SourceCandidate sc;
         sc.title       = c.displayTitle;
+        sc.matchText    = identityBlob(c);
         sc.seeders     = c.seeders;
         sc.sizeBytes   = c.sizeBytes;
         sc.qualitySort = c.qualitySort;
         cands.append(sc);
     }
 
-    // DOWNLOAD BUG 2026-06-02 ? diagnostic: dump every candidate with whether
-    // it would pass the show-identity gate. If the pick still fails, this tells
-    // us conclusively whether real One Piece titles are being wrongly rejected
-    // (gate too strict) vs the choices being the wrong show entirely
-    // (correlation still broken ? wrong streams reached this handler).
-    for (const auto& ch : choices)
+    // DOWNLOAD BUG 2026-06-02/06-03 ? diagnostic: dump every candidate with
+    // whether it passes the show-identity gate against the RAW identity blob
+    // (what pick() now uses) vs the old displayTitle. If the pick still fails,
+    // this shows conclusively whether real One Piece titles are being wrongly
+    // rejected (gate too strict / blob missing the name) vs the choices being
+    // the wrong show entirely (correlation broken ? wrong streams reached here).
+    for (const auto& ch : choices) {
+        const QString blob = identityBlob(ch);
         qInfo().noquote() << "[auto-dl] cand q=" << ch.qualitySort << "seed=" << ch.seeders
-                          << "gate=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
+                          << "gateBlob=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
+                                            blob, ctx.showTitle)
+                          << "gateOld=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
                                             ch.displayTitle, ctx.showTitle)
-                          << "title=" << ch.displayTitle.left(80);
+                          << "title=" << ch.displayTitle.left(60)
+                          << "| blob=" << blob.simplified().left(100);
+    }
 
     const std::optional<int> picked =
         tankostream::stream::AutoSourcePicker::pick(cands, ctx.showTitle, ctx.runtimeMinutes);
diff --git a/tests/core/stream/test_auto_source_picker.cpp b/tests/core/stream/test_auto_source_picker.cpp
index a5b5b00..dfae454 100644
--- a/tests/core/stream/test_auto_source_picker.cpp
+++ b/tests/core/stream/test_auto_source_picker.cpp
@@ -132,3 +132,65 @@ TEST(AutoSourcePicker, EmptyShowTitleSkipsGate) {
     EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());            // int overload
     EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 24).has_value()); // empty gate
 }
+
+// ?? matchText identity blob (DOWNLOAD BUG 2026-06-03) ???????????????????????
+// NyaaSi / anime-shaped addons put a stats badge ("[2176 seeders | 1.34 GB |
+// NyaaSi]") in the display title, so gating on `title` rejected EVERY real One Piece
+// result -> "No 1080p source found" even though the right show's sources
+// arrived. The gate now matches `matchText` (full raw identity blob) when set,
+// falling back to `title` when empty.
+
+static SourceCandidate cm(const QString& title, const QString& matchText,
+                          int seeders, qint64 sizeBytes, int qualitySort) {
+    SourceCandidate s; s.title = title; s.matchText = matchText;
+    s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
+}
+
+TEST(AutoSourcePicker, MatchTextLetsBadgeTitledOnePiecePassGate) {
+    // title is the useless stats badge; the real filename lives in matchText.
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]"),
+           QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
+           2176, 1400000000LL, 3),
+    };
+    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
+    ASSERT_TRUE(idx.has_value()) << "matchText carries 'One Piece' -> must pass the gate";
+    EXPECT_EQ(*idx, 0);
+}
+
+TEST(AutoSourcePicker, MatchTextStillRejectsWrongShowBadge) {
+    // Badge title AND a matchText for the wrong show -> still rejected (the gate
+    // is not weakened, it just looks at the right text).
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
+           QStringLiteral("[5000 seeders | 50 GB | NyaaSi]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
+           5000, 50000000000LL, 3),
+    };
+    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
+}
+
+TEST(AutoSourcePicker, MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
+           QStringLiteral("[5000 seeders]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
+           5000, 50000000000LL, 3),
+        cm(QStringLiteral("[800 seeders | 1.34 GB | NyaaSi]"),
+           QStringLiteral("[800 seeders]\n[Erai-raws] One Piece - 1164 [1080p].mkv"),
+           800, 1400000000LL, 3),
+    };
+    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
+    ASSERT_TRUE(idx.has_value());
+    EXPECT_EQ(*idx, 1) << "must skip the better-seeded wrong-show badge and pick One Piece";
+}
+
+TEST(AutoSourcePicker, EmptyMatchTextFallsBackToTitleGate) {
+    // matchText empty -> gate uses title (unchanged legacy behavior).
+    QList<SourceCandidate> v {
+        c(QStringLiteral("One Piece - 1164 [1080p].mkv"), 800, 1400000000LL, 3),
+    };
+    EXPECT_TRUE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
+    QList<SourceCandidate> w {
+        c(QStringLiteral("Community.S01.1080p.mkv"), 800, 1400000000LL, 3),
+    };
+    EXPECT_FALSE(AutoSourcePicker::pick(w, QStringLiteral("One Piece"), 24).has_value());
+}


codex
I’ll keep this read-only and verify the live diff plus the affected literals. I’m checking the actual files rather 
than relying only on the pasted diff.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'rg -n "2176|5000|800|One 
Piece|Community|matchText|identity blob|DOWNLOAD BUG" tests/core/stream/test_auto_source_picker.cpp 
src/core/stream/AutoSourcePicker.cpp src/core/stream/AutoSourcePicker.h src/ui/pages/StreamPage.cpp' in 
C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'git diff -- 
src/core/stream/AutoSourcePicker.cpp src/core/stream/AutoSourcePicker.h src/ui/pages/StreamPage.cpp 
tests/core/stream/test_auto_source_picker.cpp' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n 
\"[\\x{1F300}-\\x{1FAFF}\\x{2600}-\\x{27BF}]\" src/core/stream/AutoSourcePicker.cpp src/core/stream/AutoSourcePicker.h 
src/ui/pages/StreamPage.cpp tests/core/stream/test_auto_source_picker.cpp" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 2257ms:
src/core/stream/AutoSourcePicker.cpp:73:        // show, no matter how well-seeded (the One Piece -> Community bug).
src/core/stream/AutoSourcePicker.cpp:74:        // DOWNLOAD BUG 2026-06-03 — gate on matchText (full raw identity blob)
src/core/stream/AutoSourcePicker.cpp:78:        const QString& idText = cand.matchText.isEmpty() ? cand.title : 
cand.matchText;
tests/core/stream/test_auto_source_picker.cpp:13:        c("One Piece S01E01 [SubsPlease] 1080p", 1200, 1400000000LL, 
3),
tests/core/stream/test_auto_source_picker.cpp:14:        c("One Piece S01E01 1080p WEB-DL",          300, 
1500000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:24:        c("Show S01E01 2160p", 800, 9000000000LL, 5),
tests/core/stream/test_auto_source_picker.cpp:53:    QList<SourceCandidate> v { c("Show S01E01 1080p", 4, 180000000LL, 
3) };
tests/core/stream/test_auto_source_picker.cpp:70:    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 180000000LL, 
3) };
tests/core/stream/test_auto_source_picker.cpp:95:// ── Show-identity gate (DOWNLOAD BUG 2026-06-02) 
────────────────────────────
tests/core/stream/test_auto_source_picker.cpp:96:// Clicking Download on One Piece downloaded 'Community' because the 
picker
tests/core/stream/test_auto_source_picker.cpp:103:        c("Community.S01-S06.COMPLETE.1080p.BluRay.x265-POIASD", 
5000, 50000000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:104:        c("One Piece - 1164 [1080p].mkv",                         
800,  1400000000LL,  3),
tests/core/stream/test_auto_source_picker.cpp:106:    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24);
tests/core/stream/test_auto_source_picker.cpp:108:    EXPECT_EQ(*idx, 1) << "must skip the better-seeded Community 
pack and pick One Piece";
tests/core/stream/test_auto_source_picker.cpp:113:        c("Community.S01-S06.COMPLETE.1080p.BluRay.x265", 5000, 
50000000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:116:    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24).has_value());
tests/core/stream/test_auto_source_picker.cpp:121:        QStringLiteral("One.Piece.1164.1080p.WEB-DL"), 
QStringLiteral("One Piece")));
tests/core/stream/test_auto_source_picker.cpp:123:        QStringLiteral("[Erai-raws] One Piece - 1164 [1080p]"), 
QStringLiteral("One Piece")));
tests/core/stream/test_auto_source_picker.cpp:125:        QStringLiteral("Community.S01-S06.COMPLETE.1080p"), 
QStringLiteral("One Piece")));
tests/core/stream/test_auto_source_picker.cpp:127:        QStringLiteral("One.Tree.Hill.S01.1080p"), 
QStringLiteral("One Piece")));
tests/core/stream/test_auto_source_picker.cpp:136:// ── matchText identity blob (DOWNLOAD BUG 2026-06-03) 
───────────────────────
tests/core/stream/test_auto_source_picker.cpp:137:// NyaaSi / anime-shaped addons put a stats badge ("[2176 seeders | 
1.34 GB |
tests/core/stream/test_auto_source_picker.cpp:138:// NyaaSi]") in the display title, so gating on `title` rejected 
EVERY real One Piece
tests/core/stream/test_auto_source_picker.cpp:140:// arrived. The gate now matches `matchText` (full raw identity 
blob) when set,
tests/core/stream/test_auto_source_picker.cpp:143:static SourceCandidate cm(const QString& title, const QString& 
matchText,
tests/core/stream/test_auto_source_picker.cpp:145:    SourceCandidate s; s.title = title; s.matchText = matchText;
tests/core/stream/test_auto_source_picker.cpp:150:    // title is the useless stats badge; the real filename lives in 
matchText.
tests/core/stream/test_auto_source_picker.cpp:152:        cm(QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]"),
tests/core/stream/test_auto_source_picker.cpp:153:           QStringLiteral("[2176 seeders | 1.34 GB | 
NyaaSi]\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
tests/core/stream/test_auto_source_picker.cpp:154:           2176, 1400000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:156:    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24);
tests/core/stream/test_auto_source_picker.cpp:157:    ASSERT_TRUE(idx.has_value()) << "matchText carries 'One Piece' 
-> must pass the gate";
tests/core/stream/test_auto_source_picker.cpp:162:    // Badge title AND a matchText for the wrong show -> still 
rejected (the gate
tests/core/stream/test_auto_source_picker.cpp:165:        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
tests/core/stream/test_auto_source_picker.cpp:166:           QStringLiteral("[5000 seeders | 50 GB | 
NyaaSi]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
tests/core/stream/test_auto_source_picker.cpp:167:           5000, 50000000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:169:    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24).has_value());
tests/core/stream/test_auto_source_picker.cpp:174:        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
tests/core/stream/test_auto_source_picker.cpp:175:           QStringLiteral("[5000 
seeders]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
tests/core/stream/test_auto_source_picker.cpp:176:           5000, 50000000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:177:        cm(QStringLiteral("[800 seeders | 1.34 GB | NyaaSi]"),
tests/core/stream/test_auto_source_picker.cpp:178:           QStringLiteral("[800 seeders]\n[Erai-raws] One Piece - 
1164 [1080p].mkv"),
tests/core/stream/test_auto_source_picker.cpp:179:           800, 1400000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:181:    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24);
tests/core/stream/test_auto_source_picker.cpp:183:    EXPECT_EQ(*idx, 1) << "must skip the better-seeded wrong-show 
badge and pick One Piece";
tests/core/stream/test_auto_source_picker.cpp:187:    // matchText empty -> gate uses title (unchanged legacy 
behavior).
tests/core/stream/test_auto_source_picker.cpp:189:        c(QStringLiteral("One Piece - 1164 [1080p].mkv"), 800, 
1400000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:191:    EXPECT_TRUE(AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24).has_value());
tests/core/stream/test_auto_source_picker.cpp:193:        c(QStringLiteral("Community.S01.1080p.mkv"), 800, 
1400000000LL, 3),
tests/core/stream/test_auto_source_picker.cpp:195:    EXPECT_FALSE(AutoSourcePicker::pick(w, QStringLiteral("One 
Piece"), 24).has_value());
src/core/stream/AutoSourcePicker.h:16:    // DOWNLOAD BUG 2026-06-03 — text the show-identity gate matches against.
src/core/stream/AutoSourcePicker.h:18:    // anime-shaped Torrentio) put a stats badge ("[2176 seeders | 1.34 GB |
src/core/stream/AutoSourcePicker.h:20:    // One Piece result ("No 1080p source found"). matchText carries the full
src/core/stream/AutoSourcePicker.h:21:    // raw identity blob (name + description + fileNameHint + …) so the gate can
src/core/stream/AutoSourcePicker.h:24:    QString matchText;
src/core/stream/AutoSourcePicker.h:37:    // Show-identity gate (DOWNLOAD BUG 2026-06-02): when showTitle is non-empty,
src/core/stream/AutoSourcePicker.h:39:    // rejected BEFORE ranking — so a One Piece request can never download a
src/core/stream/AutoSourcePicker.h:40:    // 'Community' pack just because it is better-seeded. Empty showTitle = no 
gate
src/ui/pages/StreamPage.cpp:572:    timer.start(qBound(1000, timeoutMs, 15000));
src/ui/pages/StreamPage.cpp:1333:    // Live-search debounce REMOVED 2026-04-25 per Hemanth: even at 800 ms
src/ui/pages/StreamPage.cpp:1334:    // (raised from 300 â†’ 800 ms on 2026-04-20 for the same reason) the
src/ui/pages/StreamPage.cpp:2326:    // DOWNLOAD BUG 2026-06-02 — same correlation token as the auto-download
src/ui/pages/StreamPage.cpp:2372:            // Hemanth UX call (Phase 1 telemetry session, post One Piece
src/ui/pages/StreamPage.cpp:2424:    // DOWNLOAD BUG 2026-06-02 — fill the correlation token captured by the
src/ui/pages/StreamPage.cpp:2516:            // DOWNLOAD BUG 2026-06-02 — same correlation token guard as the
src/ui/pages/StreamPage.cpp:3268:    // DOWNLOAD BUG 2026-06-02 — in-flight dedup. Rapid identical Download
src/ui/pages/StreamPage.cpp:3301:            // DOWNLOAD BUG 2026-06-02 — ignore a stale emit from a superseded
src/ui/pages/StreamPage.cpp:3342:    // DOWNLOAD BUG 2026-06-02 — capture the generation token load() stamped so
src/ui/pages/StreamPage.cpp:3362:    // DOWNLOAD BUG 2026-06-03 — build the show-identity match text from the RAW
src/ui/pages/StreamPage.cpp:3364:    // put a stats badge ("[2176 seeders | 1.34 GB | NyaaSi]") in displayTitle,
src/ui/pages/StreamPage.cpp:3365:    // which lacks the show name and made the gate reject every real One Piece result
src/ui/pages/StreamPage.cpp:3368:    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
src/ui/pages/StreamPage.cpp:3388:        sc.matchText    = identityBlob(c);
src/ui/pages/StreamPage.cpp:3395:    // DOWNLOAD BUG 2026-06-02/06-03 — diagnostic: dump every candidate with
src/ui/pages/StreamPage.cpp:3396:    // whether it passes the show-identity gate against the RAW identity blob
src/ui/pages/StreamPage.cpp:3398:    // this shows conclusively whether real One Piece titles are being wrongly
src/ui/pages/StreamPage.cpp:3614:        metaTimer.start(15000);
src/ui/pages/StreamPage.cpp:3683:        timer.start(55000);
src/ui/pages/StreamPage.cpp:3721:        timer.start(15000);

 succeeded in 2276ms:
src/ui/pages/StreamPage.cpp:1285:    m_settingsBtn = new QPushButton(QStringLiteral("⚙"), m_searchBarFrame);

 succeeded in 2855ms:
diff --git a/src/core/stream/AutoSourcePicker.cpp b/src/core/stream/AutoSourcePicker.cpp
index c20e337..5dc455d 100644
--- a/src/core/stream/AutoSourcePicker.cpp
+++ b/src/core/stream/AutoSourcePicker.cpp
@@ -71,7 +71,12 @@ std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidat
         if (isCamRip(cand.title)) continue;                       // camcorder rip
         // Show-identity gate: never pick a release that isn't the requested
         // show, no matter how well-seeded (the One Piece -> Community bug).
-        if (!showTitle.isEmpty() && !titleMatchesShow(cand.title, showTitle)) continue;
+        // DOWNLOAD BUG 2026-06-03 — gate on matchText (full raw identity blob)
+        // when present; some addons put a stats badge in `title`, which lacks
+        // the show name and rejected every real result. Falls back to `title`
+        // so callers/tests that only set `title` are unaffected.
+        const QString& idText = cand.matchText.isEmpty() ? cand.title : cand.matchText;
+        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) continue;
         survivors.append(i);
     }
     if (survivors.isEmpty()) return std::nullopt;                 // no source found
diff --git a/src/core/stream/AutoSourcePicker.h b/src/core/stream/AutoSourcePicker.h
index ccc83b3..0138c61 100644
--- a/src/core/stream/AutoSourcePicker.h
+++ b/src/core/stream/AutoSourcePicker.h
@@ -12,6 +12,16 @@ struct SourceCandidate {
     int     seeders = 0;
     qint64  sizeBytes = 0;
     int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown
+
+    // DOWNLOAD BUG 2026-06-03 — text the show-identity gate matches against.
+    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
+    // anime-shaped Torrentio) put a stats badge ("[2176 seeders | 1.34 GB |
+    // NyaaSi]") there instead of the filename, so gating on `title` rejected every
+    // One Piece result ("No 1080p source found"). matchText carries the full
+    // raw identity blob (name + description + fileNameHint + …) so the gate can
+    // find the show name wherever the addon stashed it. Empty == fall back to
+    // `title` (preserves every existing caller + test that only sets `title`).
+    QString matchText;
 };
 
 // Silent best-source selection for Theatre's one-tap download.
diff --git a/src/ui/pages/StreamPage.cpp b/src/ui/pages/StreamPage.cpp
index f5a856f..c8644b5 100644
--- a/src/ui/pages/StreamPage.cpp
+++ b/src/ui/pages/StreamPage.cpp
@@ -3359,27 +3359,55 @@ void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>&
     qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                       << "active=" << (m_pendingAuto.active ? "y" : "n");
 
+    // DOWNLOAD BUG 2026-06-03 — build the show-identity match text from the RAW
+    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
+    // put a stats badge ("[2176 seeders | 1.34 GB | NyaaSi]") in displayTitle,
+    // which lacks the show name and made the gate reject every real One Piece result
+    // ("No 1080p source found"). The raw blob carries the filename wherever the
+    // addon stashed it (name / description / fileNameHint / parsedFilename), so
+    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
+    // title (its job in the picker is unchanged).
+    auto identityBlob = [](const tankostream::stream::StreamPickerChoice& ch) {
+        return QStringList{
+            ch.displayTitle,
+            ch.stream.name,
+            ch.stream.description,
+            ch.stream.source.fileNameHint,
+            ch.fileNameHint,
+            ch.stream.behaviorHints.filename,
+            ch.stream.behaviorHints.other
+                .value(QStringLiteral("parsedFilename")).toString(),
+        }.join(QLatin1Char('\n'));
+    };
+
     QList<tankostream::stream::SourceCandidate> cands;
     cands.reserve(choices.size());
     for (const auto& c : choices) {
         tankostream::stream::SourceCandidate sc;
         sc.title       = c.displayTitle;
+        sc.matchText    = identityBlob(c);
         sc.seeders     = c.seeders;
         sc.sizeBytes   = c.sizeBytes;
         sc.qualitySort = c.qualitySort;
         cands.append(sc);
     }
 
-    // DOWNLOAD BUG 2026-06-02 — diagnostic: dump every candidate with whether
-    // it would pass the show-identity gate. If the pick still fails, this tells
-    // us conclusively whether real One Piece titles are being wrongly rejected
-    // (gate too strict) vs the choices being the wrong show entirely
-    // (correlation still broken — wrong streams reached this handler).
-    for (const auto& ch : choices)
+    // DOWNLOAD BUG 2026-06-02/06-03 — diagnostic: dump every candidate with
+    // whether it passes the show-identity gate against the RAW identity blob
+    // (what pick() now uses) vs the old displayTitle. If the pick still fails,
+    // this shows conclusively whether real One Piece titles are being wrongly
+    // rejected (gate too strict / blob missing the name) vs the choices being
+    // the wrong show entirely (correlation broken — wrong streams reached here).
+    for (const auto& ch : choices) {
+        const QString blob = identityBlob(ch);
         qInfo().noquote() << "[auto-dl] cand q=" << ch.qualitySort << "seed=" << ch.seeders
-                          << "gate=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
+                          << "gateBlob=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
+                                            blob, ctx.showTitle)
+                          << "gateOld=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
                                             ch.displayTitle, ctx.showTitle)
-                          << "title=" << ch.displayTitle.left(80);
+                          << "title=" << ch.displayTitle.left(60)
+                          << "| blob=" << blob.simplified().left(100);
+    }
 
     const std::optional<int> picked =
         tankostream::stream::AutoSourcePicker::pick(cands, ctx.showTitle, ctx.runtimeMinutes);
diff --git a/tests/core/stream/test_auto_source_picker.cpp b/tests/core/stream/test_auto_source_picker.cpp
index a5b5b00..dfae454 100644
--- a/tests/core/stream/test_auto_source_picker.cpp
+++ b/tests/core/stream/test_auto_source_picker.cpp
@@ -132,3 +132,65 @@ TEST(AutoSourcePicker, EmptyShowTitleSkipsGate) {
     EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());            // int overload
     EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 24).has_value()); // empty gate
 }
+
+// ── matchText identity blob (DOWNLOAD BUG 2026-06-03) ───────────────────────
+// NyaaSi / anime-shaped addons put a stats badge ("[2176 seeders | 1.34 GB |
+// NyaaSi]") in the display title, so gating on `title` rejected EVERY real One Piece
+// result -> "No 1080p source found" even though the right show's sources
+// arrived. The gate now matches `matchText` (full raw identity blob) when set,
+// falling back to `title` when empty.
+
+static SourceCandidate cm(const QString& title, const QString& matchText,
+                          int seeders, qint64 sizeBytes, int qualitySort) {
+    SourceCandidate s; s.title = title; s.matchText = matchText;
+    s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
+}
+
+TEST(AutoSourcePicker, MatchTextLetsBadgeTitledOnePiecePassGate) {
+    // title is the useless stats badge; the real filename lives in matchText.
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]"),
+           QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
+           2176, 1400000000LL, 3),
+    };
+    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
+    ASSERT_TRUE(idx.has_value()) << "matchText carries 'One Piece' -> must pass the gate";
+    EXPECT_EQ(*idx, 0);
+}
+
+TEST(AutoSourcePicker, MatchTextStillRejectsWrongShowBadge) {
+    // Badge title AND a matchText for the wrong show -> still rejected (the gate
+    // is not weakened, it just looks at the right text).
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
+           QStringLiteral("[5000 seeders | 50 GB | NyaaSi]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
+           5000, 50000000000LL, 3),
+    };
+    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
+}
+
+TEST(AutoSourcePicker, MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
+           QStringLiteral("[5000 seeders]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
+           5000, 50000000000LL, 3),
+        cm(QStringLiteral("[800 seeders | 1.34 GB | NyaaSi]"),
+           QStringLiteral("[800 seeders]\n[Erai-raws] One Piece - 1164 [1080p].mkv"),
+           800, 1400000000LL, 3),
+    };
+    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
+    ASSERT_TRUE(idx.has_value());
+    EXPECT_EQ(*idx, 1) << "must skip the better-seeded wrong-show badge and pick One Piece";
+}
+
+TEST(AutoSourcePicker, EmptyMatchTextFallsBackToTitleGate) {
+    // matchText empty -> gate uses title (unchanged legacy behavior).
+    QList<SourceCandidate> v {
+        c(QStringLiteral("One Piece - 1164 [1080p].mkv"), 800, 1400000000LL, 3),
+    };
+    EXPECT_TRUE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
+    QList<SourceCandidate> w {
+        c(QStringLiteral("Community.S01.1080p.mkv"), 800, 1400000000LL, 3),
+    };
+    EXPECT_FALSE(AutoSourcePicker::pick(w, QStringLiteral("One Piece"), 24).has_value());
+}
warning: in the working copy of 'src/core/stream/AutoSourcePicker.cpp', LF will be replaced by CRLF the next time Git 
touches it
warning: in the working copy of 'src/core/stream/AutoSourcePicker.h', LF will be replaced by CRLF the next time Git 
touches it
warning: in the working copy of 'tests/core/stream/test_auto_source_picker.cpp', LF will be replaced by CRLF the next 
time Git touches it

codex
APPROVE
tokens used
46,823
APPROVE
