codex.cmd : Reading prompt from stdin...
At line:1 char:397
+ ... e("DIFF_PLACEHOLDER", $diff); $prompt | & codex.cmd exec 2>&1 | Out-F ...
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
session id: 019e8cca-cfb8-7421-b7aa-d71b98087a21
--------
user
Cross-model review for Tankoban 2 (requested by Agent 4, author = Agent 4/Opus). You are a DIFFERENT model than the 
author. Read-only review ??? do NOT edit any file; just report.

CONTEXT: Live-smoke follow-up to this morning's A001 stream-load-correlation fix. Hemanth clicked Download on One 
Piece episode 1164 and got "No 1080p source found". The [auto-dl] diagnostic logging (left in for exactly this) proved:
  - The request correlated to the RIGHT show: `req.id= kitsu:12:1164 showTitle= One Piece`, and 10 real One Piece 
torrents arrived (NyaaSi/ThePirateBay, up to 2176 seeders). So A001 (correlation) is WORKING ??? the right sources 
reached the handler.
  - But EVERY candidate logged `gate= false` and the pick returned NONE. Root cause: the auto-download show-identity 
gate (AutoSourcePicker show-token match) was fed `StreamPickerChoice.displayTitle`, which for these 
NyaaSi/anime-shaped addons is a STATS BADGE ("???? 2176 ???? 1.34 GB ?????? NyaaSi"), not the release filename. The 
badge contains neither "one" nor "piece", so titleMatchesShow rejected all 10 ??? "No 1080p source found".

This is a separate, older bug behind the (now-fixed) correlation race. The gate is correct in intent (it exists to 
stop "One Piece ??? Community" wrong-show auto-downloads); it was just matching the wrong field.

THE FIX UNDER REVIEW (4 files):
  1. AutoSourcePicker.h ??? add `QString matchText;` to SourceCandidate. Doc: the text the identity gate matches 
against; empty == fall back to `title`.
  2. AutoSourcePicker.cpp ??? in the gated pick(), the show-identity check now reads `matchText` when non-empty, else 
`title`:
        const QString& idText = cand.matchText.isEmpty() ? cand.title : cand.matchText;
        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) continue;
     `cand.title` is STILL used for isCamRip() and the sourceScore() tiebreak ??? unchanged.
  3. StreamPage.cpp finishAutoDownloadPick() ??? populate sc.matchText with a raw identity blob joined from 
displayTitle + stream.name + stream.description + stream.source.fileNameHint + choice.fileNameHint + 
behaviorHints.filename + behaviorHints.other["parsedFilename"]. sc.title stays displayTitle. The [auto-dl] diagnostic 
is enhanced to log gateBlob vs gateOld + the blob, so the re-smoke is conclusive.
  4. test_auto_source_picker.cpp ??? 4 new tests: badge-title + matchText-with-show passes; badge + wrong-show 
matchText still rejected; better-seeded wrong-show badge skipped for One Piece; empty matchText falls back to title 
gate.

VERIFY ??? do all of:
1. Does the fix actually unblock the One Piece case? With a badge `title` and a `matchText` blob containing 
"[SubsPlease] One Piece - 1164 (1080p).mkv", does titleMatchesShow(blob, "One Piece") pass and the candidate get 
picked?
2. Is the wrong-show protection PRESERVED (not weakened)? A blob is a multi-field join ??? could a wrong-show release 
now FALSE-MATCH because some unrelated field happens to contain the show's tokens (e.g. a "related: One Piece" 
mention, addon name, or a pack that lists many shows)? Assess the realistic risk given titleMatchesShow requires EVERY 
significant show token as a whole word.
3. Fallback correctness: when matchText is empty, is behavior byte-for-byte identical to before (gate on title)? 
Confirm no existing caller other than finishAutoDownloadPick sets matchText (it defaults empty).
4. The identityBlob lambda in StreamPage.cpp ??? any field that doesn't exist / wrong accessor / null-deref risk on 
stream.behaviorHints.other? Is joining with '\n' safe for titleMatchesShow's normalization (it replaces 
non-alphanumerics with spaces, so newlines are fine ??? confirm)?
5. Does feeding the blob to the gate interact badly with isCamRip or the sourceScore tiebreak? (It should NOT ??? 
those still read cand.title, not matchText. Confirm.)
6. Any scope creep or NEW bug introduced by the struct field / lambda / diagnostic change.
7. Sanity-check the 4 new tests actually encode the contract (no tautologies; the badge cases would FAIL against the 
old title-only gate).

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Default REQUEST-CHANGES if the fix 
doesn't unblock One Piece, weakens wrong-show protection, or you are unsure.

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
index ccc83b3..de5ca66 100644
--- a/src/core/stream/AutoSourcePicker.h
+++ b/src/core/stream/AutoSourcePicker.h
@@ -12,6 +12,16 @@ struct SourceCandidate {
     int     seeders = 0;
     qint64  sizeBytes = 0;
     int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown
+
+    // DOWNLOAD BUG 2026-06-03 ? text the show-identity gate matches against.
+    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
+    // anime-shaped Torrentio) put a stats badge ("?? 2176 ?? 1.34 GB ?? NyaaSi")
+    // there instead of the filename, so gating on `title` rejected EVERY real
+    // One Piece result ("No 1080p source found"). matchText carries the full
+    // raw identity blob (name + description + fileNameHint + ?) so the gate can
+    // find the show name wherever the addon stashed it. Empty == fall back to
+    // `title` (preserves every existing caller + test that only sets `title`).
+    QString matchText;
 };
 
 // Silent best-source selection for Theatre's one-tap download.
diff --git a/src/ui/pages/StreamPage.cpp b/src/ui/pages/StreamPage.cpp
index f5a856f..a723c04 100644
--- a/src/ui/pages/StreamPage.cpp
+++ b/src/ui/pages/StreamPage.cpp
@@ -3359,27 +3359,55 @@ void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>&
     qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                       << "active=" << (m_pendingAuto.active ? "y" : "n");
 
+    // DOWNLOAD BUG 2026-06-03 ? build the show-identity match text from the RAW
+    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
+    // put a stats badge ("?? 2176 ?? 1.34 GB ?? NyaaSi") in displayTitle, which
+    // lacks the show name and made the gate reject every real One Piece result
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
index a5b5b00..622bf39 100644
--- a/tests/core/stream/test_auto_source_picker.cpp
+++ b/tests/core/stream/test_auto_source_picker.cpp
@@ -132,3 +132,65 @@ TEST(AutoSourcePicker, EmptyShowTitleSkipsGate) {
     EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());            // int overload
     EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 24).has_value()); // empty gate
 }
+
+// ?? matchText identity blob (DOWNLOAD BUG 2026-06-03) ???????????????????????
+// NyaaSi / anime-shaped addons put a stats badge ("?? 2176 ?? 1.34 GB ?? NyaaSi")
+// in the display title, so gating on `title` rejected EVERY real One Piece
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
+        cm(QStringLiteral("?? 2176 ?? 1.34 GB ?? NyaaSi"),
+           QStringLiteral("?? 2176 ?? 1.34 GB ?? NyaaSi\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
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
+        cm(QStringLiteral("?? 5000 ?? 50 GB ?? NyaaSi"),
+           QStringLiteral("?? 5000 ?? 50 GB ?? NyaaSi\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
+           5000, 50000000000LL, 3),
+    };
+    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
+}
+
+TEST(AutoSourcePicker, MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("?? 5000 ?? 50 GB ?? NyaaSi"),
+           QStringLiteral("?? 5000\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
+           5000, 50000000000LL, 3),
+        cm(QStringLiteral("?? 800 ?? 1.34 GB ?? NyaaSi"),
+           QStringLiteral("?? 800\n[Erai-raws] One Piece - 1164 [1080p].mkv"),
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
I’ll keep this read-only and verify the diff against the current picker, stream choice types, and existing callers so 
the review is grounded in the repo state rather than the pasted patch alone.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'rg -n "struct StreamPickerChoice|class 
StreamPickerChoice|behaviorHints|fileNameHint|parsedFilename|finishAutoDownloadPick|displayTitle|SourceCandidate" src 
tests -S' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'git diff -- 
src/core/stream/AutoSourcePicker.cpp src/core/stream/AutoSourcePicker.h src/ui/pages/StreamPage.cpp 
tests/core/stream/test_auto_source_picker.cpp' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"struct SourceCandidate|class 
AutoSourcePicker|titleMatchesShow|pick\\(\" src tests -S" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 2526ms:
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
index ccc83b3..de5ca66 100644
--- a/src/core/stream/AutoSourcePicker.h
+++ b/src/core/stream/AutoSourcePicker.h
@@ -12,6 +12,16 @@ struct SourceCandidate {
     int     seeders = 0;
     qint64  sizeBytes = 0;
     int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown
+
+    // DOWNLOAD BUG 2026-06-03 — text the show-identity gate matches against.
+    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
+    // anime-shaped Torrentio) put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi")
+    // there instead of the filename, so gating on `title` rejected EVERY real
+    // One Piece result ("No 1080p source found"). matchText carries the full
+    // raw identity blob (name + description + fileNameHint + …) so the gate can
+    // find the show name wherever the addon stashed it. Empty == fall back to
+    // `title` (preserves every existing caller + test that only sets `title`).
+    QString matchText;
 };
 
 // Silent best-source selection for Theatre's one-tap download.
diff --git a/src/ui/pages/StreamPage.cpp b/src/ui/pages/StreamPage.cpp
index f5a856f..a723c04 100644
--- a/src/ui/pages/StreamPage.cpp
+++ b/src/ui/pages/StreamPage.cpp
@@ -3359,27 +3359,55 @@ void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>&
     qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                       << "active=" << (m_pendingAuto.active ? "y" : "n");
 
+    // DOWNLOAD BUG 2026-06-03 — build the show-identity match text from the RAW
+    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
+    // put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi") in displayTitle, which
+    // lacks the show name and made the gate reject every real One Piece result
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
index a5b5b00..622bf39 100644
--- a/tests/core/stream/test_auto_source_picker.cpp
+++ b/tests/core/stream/test_auto_source_picker.cpp
@@ -132,3 +132,65 @@ TEST(AutoSourcePicker, EmptyShowTitleSkipsGate) {
     EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());            // int overload
     EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 24).has_value()); // empty gate
 }
+
+// ── matchText identity blob (DOWNLOAD BUG 2026-06-03) ───────────────────────
+// NyaaSi / anime-shaped addons put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi")
+// in the display title, so gating on `title` rejected EVERY real One Piece
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
+        cm(QStringLiteral("👤 2176 💾 1.34 GB ⚙️ NyaaSi"),
+           QStringLiteral("👤 2176 💾 1.34 GB ⚙️ NyaaSi\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
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
+        cm(QStringLiteral("👤 5000 💾 50 GB ⚙️ NyaaSi"),
+           QStringLiteral("👤 5000 💾 50 GB ⚙️ NyaaSi\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
+           5000, 50000000000LL, 3),
+    };
+    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
+}
+
+TEST(AutoSourcePicker, MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
+    QList<SourceCandidate> v {
+        cm(QStringLiteral("👤 5000 💾 50 GB ⚙️ NyaaSi"),
+           QStringLiteral("👤 5000\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
+           5000, 50000000000LL, 3),
+        cm(QStringLiteral("👤 800 💾 1.34 GB ⚙️ NyaaSi"),
+           QStringLiteral("👤 800\n[Erai-raws] One Piece - 1164 [1080p].mkv"),
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

 succeeded in 3390ms:
src\ui\player\VideoPlayer.h:56:                  const QString& displayTitle = {});
src\ui\pages\ComicsPage.h:148:                                        const QString& displayTitle) const;
src\ui\pages\ComicsPage.h:156:    QString resolveReadVolumeCover(const QString& displayTitle,
src\ui\pages\ComicsPage.h:379:                                                  const QString& displayTitle);
src\ui\player\VideoPlayer.cpp:285:                            const QString& displayTitle)
src\ui\player\VideoPlayer.cpp:309:    // etc.), which is useless as a title. Callers pass displayTitle to
src\ui\player\VideoPlayer.cpp:317:    const QString rawForLabel = !displayTitle.isEmpty() ? displayTitle : filePath;
src\ui\pages\ComicsPage.cpp:2146:// Looks up the series in m_localCatalogIndex by anilistId or displayTitle,
src\ui\pages\ComicsPage.cpp:2149:                                                 const QString& displayTitle) const
src\ui\pages\ComicsPage.cpp:2156:    if (slug.isEmpty() && !displayTitle.isEmpty()) {
src\ui\pages\ComicsPage.cpp:2157:        slug = m_localCatalogIndex.slugForSeriesTitle(displayTitle);
src\ui\pages\ComicsPage.cpp:2180:QString ComicsPage::resolveReadVolumeCover(const QString& displayTitle,
src\ui\pages\ComicsPage.cpp:2183:    if (displayTitle.isEmpty() || volumeNumber <= 0)
src\ui\pages\ComicsPage.cpp:2186:    const QString slug = m_localCatalogIndex.slugForSeriesTitle(displayTitle);
src\ui\pages\ComicsPage.cpp:2802:                                             const QString& displayTitle)
src\ui\pages\ComicsPage.cpp:2808:    rec.title    = displayTitle;
src\ui\pages\ComicsPage.cpp:2842:            QString displayTitle;
src\ui\pages\ComicsPage.cpp:2864:                gt.displayTitle = resolvedTitle.isEmpty()
src\ui\pages\ComicsPage.cpp:2868:            if (gt.displayTitle.isEmpty()) {
src\ui\pages\ComicsPage.cpp:2870:                    gt.displayTitle = resolvedTitle;
src\ui\pages\ComicsPage.cpp:2873:                    gt.displayTitle = humanized.isEmpty() ? e.seriesId : humanized;
src\ui\pages\ComicsPage.cpp:2886:            if (gt.coverUrl.isEmpty() && !gt.displayTitle.isEmpty()) {
src\ui\pages\ComicsPage.cpp:2888:                                                           gt.displayTitle);
src\ui\pages\ComicsPage.cpp:2935:            if (gt.anilistId == 0 && m_anilistCache && !gt.displayTitle.isEmpty()) {
src\ui\pages\ComicsPage.cpp:2937:                const QString titleKey = gt.displayTitle.toLower().trimmed();
src\ui\pages\ComicsPage.cpp:2952:            const QString displayTitle = gt.displayTitle;
src\ui\pages\ComicsPage.cpp:2960:            if (!displayTitle.isEmpty())
src\ui\pages\ComicsPage.cpp:2961:                downloadedTitleKeysNorm.insert(displayTitle.toLower().trimmed());
src\ui\pages\ComicsPage.cpp:2963:            auto* card = new TileCard(coverPath, displayTitle, 
QStringLiteral("Downloaded"));
src\ui\pages\ComicsPage.cpp:2967:            card->setProperty("seriesName", displayTitle);
src\ui\pages\ComicsPage.cpp:2977:            const QString linkedTitle   = displayTitle;
tests\core\queue\test_transfer_queue.cpp:11:    it.displayTitle = tid;
tests\core\stream\test_auto_source_picker.cpp:5:using tankostream::stream::SourceCandidate;
tests\core\stream\test_auto_source_picker.cpp:7:static SourceCandidate c(const QString& title, int seeders, qint64 
sizeBytes, int qualitySort) {
tests\core\stream\test_auto_source_picker.cpp:8:    SourceCandidate s; s.title = title; s.seeders = seeders; 
s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
tests\core\stream\test_auto_source_picker.cpp:12:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:22:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:30:    QList<SourceCandidate> v { c("Show S01E01 1080p", 0, 
1400000000LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:41:    QList<SourceCandidate> v { c("Movie 2024 1080p CAM", 500, 
3000000000LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:46:    QList<SourceCandidate> v { c("Show S01E01 1080p", 1200, 
30000000000LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:53:    QList<SourceCandidate> v { c("Show S01E01 1080p", 4, 180000000LL, 
3) };
tests\core\stream\test_auto_source_picker.cpp:58:    QList<SourceCandidate> v { c("Show S01E01 1080p", 6, 
13000000000LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:63:    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 
1400000000LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:70:    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 180000000LL, 
3) };
tests\core\stream\test_auto_source_picker.cpp:77:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:87:    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, -1LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:102:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:112:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:131:    QList<SourceCandidate> v { c("Anything 1080p", 1000, 
1400000000LL, 3) };
tests\core\stream\test_auto_source_picker.cpp:143:static SourceCandidate cm(const QString& title, const QString& 
matchText,
tests\core\stream\test_auto_source_picker.cpp:145:    SourceCandidate s; s.title = title; s.matchText = matchText;
tests\core\stream\test_auto_source_picker.cpp:151:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:164:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:173:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:188:    QList<SourceCandidate> v {
tests\core\stream\test_auto_source_picker.cpp:192:    QList<SourceCandidate> w {
src\ui\pages\StreamPage.h:60:struct StreamPickerChoice;
src\ui\pages\StreamPage.h:343:    void finishAutoDownloadPick(const QList<tankostream::addon::Stream>& streams,
src\ui\pages\comics\VolumeTile.cpp:122:QString displayTitleForVolume(const VolumeTileData& data)
src\ui\pages\comics\VolumeTile.cpp:363:    m_titleLabel = new QLabel(displayTitleForVolume(m_data), this);
src\ui\dialogs\AddAddonDialog.h:38:    // manifest.behaviorHints.configurationRequired we branch:
src\ui\pages\comics\ComicsDownloadsPage.cpp:343:    // Card-level data: displayTitle, list of (sourceId, volumeNumber,
src\ui\pages\comics\ComicsDownloadsPage.cpp:348:        QString displayTitle;
src\ui\pages\comics\ComicsDownloadsPage.cpp:372:                    card.displayTitle = 
m_comicsPage->resolveDisplayTitle(srcId, serId);
src\ui\pages\comics\ComicsDownloadsPage.cpp:373:                    if (card.displayTitle.isEmpty()) {
src\ui\pages\comics\ComicsDownloadsPage.cpp:375:                        card.displayTitle = 
ComicsPage::humanizeSlug(serId);
src\ui\pages\comics\ComicsDownloadsPage.cpp:377:                    if (card.displayTitle.isEmpty()) {
src\ui\pages\comics\ComicsDownloadsPage.cpp:379:                        card.displayTitle = serId;
src\ui\pages\comics\ComicsDownloadsPage.cpp:382:                    card.displayTitle = serId;
src\ui\pages\comics\ComicsDownloadsPage.cpp:428:            ? m_comicsPage->resolveCanonicalSeriesCover(0, 
card.displayTitle)
src\ui\pages\comics\ComicsDownloadsPage.cpp:444:        h->addWidget(makeCoverWidget(coverUrl, card.displayTitle), 0, 
Qt::AlignTop);
src\ui\pages\comics\ComicsDownloadsPage.cpp:451:        auto* titleLabel = new QLabel(card.displayTitle, cardFrame);
src\ui\pages\comics\ComicsDownloadsPage.cpp:509:        const QString storedDisplayTitle = card.displayTitle;
src\ui\dialogs\AddAddonDialog.cpp:36:    // dialog can introspect manifest.behaviorHints.configurationRequired
src\ui\dialogs\AddAddonDialog.cpp:99:    if (descriptor.manifest.behaviorHints.configurationRequired) {
src\ui\pages\StreamPage.cpp:704:        row[QStringLiteral("name")]       = choice.displayTitle;
src\ui\pages\StreamPage.cpp:1027:                // index 0 + startPositionSec 0.0; displayTitle = show title.
src\ui\pages\StreamPage.cpp:2359:                    if (c.stream.behaviorHints.bingeGroup == seriesBingeGroup) {
src\ui\pages\StreamPage.cpp:2576:                              && c.stream.behaviorHints.bingeGroup == 
seriesBingeGroup;
src\ui\pages\StreamPage.cpp:2980:    // displayTitle to carry the release name (was: addon name). It is
src\ui\pages\StreamPage.cpp:2985:    QString displayName = choice.displayTitle;
src\ui\pages\StreamPage.cpp:3261:// auto-pick + startDownload in finishAutoDownloadPick().
src\ui\pages\StreamPage.cpp:3309:            finishAutoDownloadPick(streams, addonsById);
src\ui\pages\StreamPage.cpp:3351:void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>& 
streams,
src\ui\pages\StreamPage.cpp:3363:    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
src\ui\pages\StreamPage.cpp:3364:    // put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi") in displayTitle, which
src\ui\pages\StreamPage.cpp:3367:    // addon stashed it (name / description / fileNameHint / parsedFilename), so
src\ui\pages\StreamPage.cpp:3368:    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
src\ui\pages\StreamPage.cpp:3372:            ch.displayTitle,
src\ui\pages\StreamPage.cpp:3375:            ch.stream.source.fileNameHint,
src\ui\pages\StreamPage.cpp:3376:            ch.fileNameHint,
src\ui\pages\StreamPage.cpp:3377:            ch.stream.behaviorHints.filename,
src\ui\pages\StreamPage.cpp:3378:            ch.stream.behaviorHints.other
src\ui\pages\StreamPage.cpp:3379:                .value(QStringLiteral("parsedFilename")).toString(),
src\ui\pages\StreamPage.cpp:3383:    QList<tankostream::stream::SourceCandidate> cands;
src\ui\pages\StreamPage.cpp:3386:        tankostream::stream::SourceCandidate sc;
src\ui\pages\StreamPage.cpp:3387:        sc.title       = c.displayTitle;
src\ui\pages\StreamPage.cpp:3397:    // (what pick() now uses) vs the old displayTitle. If the pick still fails,
src\ui\pages\StreamPage.cpp:3407:                                            ch.displayTitle, ctx.showTitle)
src\ui\pages\StreamPage.cpp:3408:                          << "title=" << ch.displayTitle.left(60)
src\ui\pages\StreamPage.cpp:4143:        synth.behaviorHints.filename  = fi.fileName();
src\ui\pages\StreamPage.cpp:4144:        synth.behaviorHints.videoSize = fi.size();
src\ui\pages\StreamPage.cpp:4145:        synth.behaviorHints.videoHash = QString();   // omitted for local files
src\ui\pages\StreamPage.cpp:4218:    saved["fileNameHint"]  = choice.fileNameHint;
src\ui\pages\StreamPage.cpp:4221:    saved["quality"]       = choice.stream.behaviorHints.other
src\ui\pages\StreamPage.cpp:4223:    saved["trackerSource"] = choice.stream.behaviorHints.other
src\ui\pages\StreamPage.cpp:4225:    saved["bingeGroup"]    = choice.stream.behaviorHints.bingeGroup;
src\ui\pages\StreamPage.cpp:4233:    // declares `behaviorHints.bingeGroup` on this stream, persist a parallel
src\ui\pages\StreamPage.cpp:4238:        && !choice.stream.behaviorHints.bingeGroup.isEmpty()) {
src\ui\pages\comics\ComicsSourcesPanel.h:118:    void onNyaaResults(int reqId, const QList<NyaaSourceCandidate>& 
results);
src\ui\pages\comics\ComicsSourcesPanel.cpp:354:                                       const 
QList<NyaaSourceCandidate>& results)
src\ui\MainWindow.cpp:1435:                                            const QString& displayTitle)
src\ui\MainWindow.cpp:1451:    m_videoPlayer->openFile(filePath, {}, 0, startPositionSec, displayTitle);
src\ui\MainWindow.cpp:1526:    const QString displayTitle = isSeries
src\ui\MainWindow.cpp:1532:    openVideoPlayerWithOptions(localPath, streamResumeSec, displayTitle);
src\ui\MainWindow.h:187:                                    const QString& displayTitle);
src\ui\pages\stream\AddonDetailPanel.cpp:57:    return descriptor.manifest.behaviorHints.configurable
src\ui\pages\stream\AddonDetailPanel.cpp:374:    // behaviorHints.configurable=true (toggled in setDescriptor). Opens
src\ui\pages\stream\AddonDetailPanel.h:39:    // when manifest.behaviorHints.configurable is true.
src\ui\pages\stream\AddonDetailPanel.h:69:    // when the current descriptor's manifest.behaviorHints.configurable is
src\ui\pages\stream\CatalogBrowseScreen.h:74:    QString displayTitleForCatalog(const CatalogItem& item) const;
src\ui\pages\stream\CatalogBrowseScreen.cpp:441:        state->row = new 
CatalogRow(displayTitleForCatalog(state->item), m_scrollContent);
src\ui\pages\stream\CatalogBrowseScreen.cpp:520:        const QString text = 
normalizedCatalogText(displayTitleForCatalog(item) + QLatin1Char(' ') + item.id);
src\ui\pages\stream\CatalogBrowseScreen.cpp:544:QString CatalogBrowseScreen::displayTitleForCatalog(const CatalogItem& 
item) const
src\ui\pages\stream\CatalogBrowseScreen.cpp:586:    m_statusLabel->setText(QStringLiteral("Loading ") + 
displayTitleForCatalog(*c) +
src\ui\pages\stream\StreamSourceList.cpp:128:    if (outDisplayName) *outDisplayName = choice.displayTitle;
src\ui\pages\stream\StreamSourceChoice.h:22:// have to reach back into `stream.behaviorHints.other` for everything.
src\ui\pages\stream\StreamSourceChoice.h:23:struct StreamPickerChoice {
src\ui\pages\stream\StreamSourceChoice.h:32:    QString fileNameHint;
src\ui\pages\stream\StreamSourceChoice.h:35:    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — displayTitle semantic
src\ui\pages\stream\StreamSourceChoice.h:43:    QString     displayTitle;     // primary line — release name 
(addon-agnostic)
src\ui\pages\stream\StreamSourceChoice.cpp:39:    if 
(stream.behaviorHints.other.contains(QStringLiteral("sizeBytes"))) {
src\ui\pages\stream\StreamSourceChoice.cpp:40:        return 
stream.behaviorHints.other.value(QStringLiteral("sizeBytes")).toLongLong();
src\ui\pages\stream\StreamSourceChoice.cpp:42:    return stream.behaviorHints.videoSize;
src\ui\pages\stream\StreamSourceChoice.cpp:47:    return 
stream.behaviorHints.other.value(QStringLiteral("seeders")).toInt(0);
src\ui\pages\stream\StreamSourceChoice.cpp:52:    const QString q = stream.behaviorHints.other
src\ui\pages\stream\StreamSourceChoice.cpp:116://   1. behaviorHints.other["parsedFilename"] — Torrentio-style 
enrichment;
src\ui\pages\stream\StreamSourceChoice.cpp:118://   2. source.fileNameHint — Stremio spec field; already clean.
src\ui\pages\stream\StreamSourceChoice.cpp:119://   3. behaviorHints.filename — Stremio spec field, alternate carrier.
src\ui\pages\stream\StreamSourceChoice.cpp:137:    const QString parsed = stream.behaviorHints.other
src\ui\pages\stream\StreamSourceChoice.cpp:138:                               
.value(QStringLiteral("parsedFilename")).toString().trimmed();
src\ui\pages\stream\StreamSourceChoice.cpp:141:    if (!stream.source.fileNameHint.isEmpty())
src\ui\pages\stream\StreamSourceChoice.cpp:142:        return firstLine(stream.source.fileNameHint);
src\ui\pages\stream\StreamSourceChoice.cpp:143:    if (!stream.behaviorHints.filename.isEmpty())
src\ui\pages\stream\StreamSourceChoice.cpp:144:        return firstLine(stream.behaviorHints.filename);
src\ui\pages\stream\StreamSourceChoice.cpp:272:// behaviorHints.other["originAddonId"/"originAddonName"]; fall back to 
the
src\ui\pages\stream\StreamSourceChoice.cpp:279:    outId = stream.behaviorHints.other
src\ui\pages\stream\StreamSourceChoice.cpp:281:    outName = stream.behaviorHints.other
src\ui\pages\stream\StreamSourceChoice.cpp:330:        c.fileNameHint = stream.source.fileNameHint;
src\ui\pages\stream\StreamSourceChoice.cpp:343:            c.displayTitle = QStringLiteral("Direct stream");
src\ui\pages\stream\StreamSourceChoice.cpp:345:            c.displayTitle = release;
src\ui\pages\stream\StreamSourceChoice.cpp:356:        c.trackerSource   = stream.behaviorHints.other
src\ui\pages\stream\StreamSourceChoice.cpp:394:            // tiebreak now keys on displayTitle (release name) since
src\ui\pages\stream\StreamSourceChoice.cpp:397:            return a.displayTitle.toLower() < b.displayTitle.toLower();
src\ui\pages\stream\StreamSourceCard.cpp:93:    // Reads addonName directly now that displayTitle was repurposed to the
src\ui\pages\stream\StreamSourceCard.cpp:115:    m_titleLabel = new QLabel(m_choice.displayTitle, this);
src\ui\pages\stream\StreamSourceCard.cpp:119:    m_titleLabel->setToolTip(m_choice.displayTitle);
src\ui\pages\stream\StreamSourceCard.cpp:217:        fm.elidedText(m_choice.displayTitle, Qt::ElideRight, avail));
src\core\queue\TransferItem.h:22:    QString displayTitle;
src\core\stream\AutoSourcePicker.h:10:struct SourceCandidate {
src\core\stream\AutoSourcePicker.h:21:    // raw identity blob (name + description + fileNameHint + …) so the gate can
src\core\stream\AutoSourcePicker.h:34:    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
src\core\stream\AutoSourcePicker.h:42:    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
src\core\stream\AutoSourcePicker.cpp:57:std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& 
candidates,
src\core\stream\AutoSourcePicker.cpp:62:std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& 
candidates,
src\core\stream\AutoSourcePicker.cpp:68:        const SourceCandidate& cand = candidates.at(i);
src\core\stream\AutoSourcePicker.cpp:87:        const SourceCandidate& ca = candidates.at(a);
src\core\stream\AutoSourcePicker.cpp:88:        const SourceCandidate& cb = candidates.at(b);
src\core\stream\BulkPackVerifier.cpp:27:        if (!item.choice.displayTitle.trimmed().isEmpty()) return 
item.choice.displayTitle;
src\core\stream\addon\AddonRegistry.cpp:125:    hints[QStringLiteral("adult")] = m.behaviorHints.adult;
src\core\stream\addon\AddonRegistry.cpp:126:    hints[QStringLiteral("p2p")] = m.behaviorHints.p2p;
src\core\stream\addon\AddonRegistry.cpp:127:    hints[QStringLiteral("configurable")] = m.behaviorHints.configurable;
src\core\stream\addon\AddonRegistry.cpp:128:    hints[QStringLiteral("configurationRequired")] = 
m.behaviorHints.configurationRequired;
src\core\stream\addon\AddonRegistry.cpp:129:    for (auto it = m.behaviorHints.other.constBegin();
src\core\stream\addon\AddonRegistry.cpp:130:         it != m.behaviorHints.other.constEnd(); ++it) {
src\core\stream\addon\AddonRegistry.cpp:133:    out[QStringLiteral("behaviorHints")] = hints;
src\core\stream\addon\AddonRegistry.cpp:227:    const QJsonObject hints = 
m.value(QStringLiteral("behaviorHints")).toObject();
src\core\stream\addon\AddonRegistry.cpp:228:    out.behaviorHints.adult = 
hints.value(QStringLiteral("adult")).toBool(false);
src\core\stream\addon\AddonRegistry.cpp:229:    out.behaviorHints.p2p = 
hints.value(QStringLiteral("p2p")).toBool(false);
src\core\stream\addon\AddonRegistry.cpp:230:    out.behaviorHints.configurable = 
hints.value(QStringLiteral("configurable")).toBool(false);
src\core\stream\addon\AddonRegistry.cpp:231:    out.behaviorHints.configurationRequired =
src\core\stream\addon\AddonRegistry.cpp:242:            out.behaviorHints.other.insert(it.key(), 
it.value().toVariant());
src\core\torrent\TorrentClient.cpp:865:    item.displayTitle = hash;  // Phase 5 will resolve via show-metadata cache
src\core\torrent\TorrentClient.cpp:882:    args.displayTitle = item.displayTitle;
src\core\torrent\TorrentClient.cpp:2881:        item.displayTitle = hash;
src\core\stream\addon\AddonTransport.cpp:276:    const QJsonObject hints = 
obj.value(QStringLiteral("behaviorHints")).toObject();
src\core\stream\addon\AddonTransport.cpp:277:    out.behaviorHints.adult = 
hints.value(QStringLiteral("adult")).toBool(false);
src\core\stream\addon\AddonTransport.cpp:278:    out.behaviorHints.p2p = 
hints.value(QStringLiteral("p2p")).toBool(false);
src\core\stream\addon\AddonTransport.cpp:279:    out.behaviorHints.configurable = 
hints.value(QStringLiteral("configurable")).toBool(false);
src\core\stream\addon\AddonTransport.cpp:280:    out.behaviorHints.configurationRequired =
src\core\stream\addon\AddonTransport.cpp:291:            out.behaviorHints.other.insert(it.key(), 
it.value().toVariant());
src\core\stream\addon\MetaItem.h:72:    MetaItemBehaviorHints behaviorHints;
src\core\stream\addon\Manifest.h:57:    ManifestBehaviorHints behaviorHints;
src\core\torrent\TorrentClient.h:116:    QString displayTitle;
src\core\stream\StreamAggregator.cpp:357:    if (stream.behaviorHints.bingeGroup.isEmpty()) {
src\core\stream\StreamAggregator.cpp:411:        stream.behaviorHints.other.insert(QStringLiteral("qualityLabel"), 
quality);
src\core\stream\StreamAggregator.cpp:414:        stream.behaviorHints.other.insert(QStringLiteral("trackerSource"), 
trackerSource);
src\core\stream\StreamAggregator.cpp:417:        stream.behaviorHints.other.insert(QStringLiteral("tracker"), tracker);
src\core\stream\StreamAggregator.cpp:420:        stream.behaviorHints.other.insert(QStringLiteral("languages"), 
languages);
src\core\stream\StreamAggregator.cpp:423:        stream.behaviorHints.other.insert(QStringLiteral("seeders"), seeders);
src\core\stream\StreamAggregator.cpp:426:        stream.behaviorHints.other.insert(QStringLiteral("sizeBytes"),
src\core\stream\StreamAggregator.cpp:430:    if (!fileHintFromTitle.isEmpty() && stream.source.fileNameHint.isEmpty()) 
{
src\core\stream\StreamAggregator.cpp:431:        stream.source.fileNameHint = fileHintFromTitle;
src\core\stream\StreamAggregator.cpp:433:    if (!stream.behaviorHints.filename.isEmpty()) {
src\core\stream\StreamAggregator.cpp:434:        stream.source.fileNameHint = stream.behaviorHints.filename;
src\core\stream\StreamAggregator.cpp:441:    // anything useful per Torrentio payload. Mirror into 
behaviorHints.filename
src\core\stream\StreamAggregator.cpp:446:        stream.behaviorHints.other.insert(QStringLiteral("parsedFilename"),
src\core\stream\StreamAggregator.cpp:448:        if (stream.behaviorHints.filename.isEmpty()) {
src\core\stream\StreamAggregator.cpp:449:            stream.behaviorHints.filename = fileHintFromTitle;
src\core\stream\StreamAggregator.cpp:465:            .arg(stream.source.fileNameHint);
src\core\stream\StreamAggregator.cpp:490:    
parseBehaviorHints(streamObj.value(QStringLiteral("behaviorHints")).toObject(),
src\core\stream\StreamAggregator.cpp:491:                       out.behaviorHints);
src\core\stream\StreamAggregator.cpp:672:        parsed.behaviorHints.other.insert(QStringLiteral("originAddonId"), 
addonId);
src\core\stream\StreamAggregator.cpp:673:        parsed.behaviorHints.other.insert(QStringLiteral("originAddonName"), 
addonName);
src\core\stream\StreamBulkPlan.cpp:280:    return QString::compare(a.displayTitle, b.displayTitle, 
Qt::CaseInsensitive) < 0;
src\core\stream\StreamBulkPlan.cpp:333:    return choice.displayTitle;
src\core\stream\StreamProgress.h:170:// Stream payload carries a `behaviorHints.bingeGroup` (the addon's signal
src\core\stream\addon\StreamSource.h:23:    QString fileNameHint;
src\core\stream\addon\StreamSource.h:54:        source.fileNameHint = hint;
src\core\stream\addon\StreamInfo.h:35:    StreamBehaviorHints behaviorHints;
src\core\stream\SubtitlesAggregator.cpp:48:    const QString videoHash = stream.behaviorHints.videoHash.trimmed();
src\core\stream\SubtitlesAggregator.cpp:55:    if (stream.behaviorHints.videoSize > 0) {
src\core\stream\SubtitlesAggregator.cpp:56:        const QString sz = QString::number(stream.behaviorHints.videoSize);
src\core\stream\SubtitlesAggregator.cpp:61:    QString fileName = stream.behaviorHints.filename.trimmed();
src\core\stream\SubtitlesAggregator.cpp:63:        fileName = stream.source.fileNameHint.trimmed();
src\core\stream\SubtitlesAggregator.cpp:184:        + request.selectedStream.behaviorHints.videoHash + QLatin1Char('|')
src\core\stream\SubtitlesAggregator.cpp:185:        + QString::number(request.selectedStream.behaviorHints.videoSize) 
+ QLatin1Char('|')
src\core\stream\SubtitlesAggregator.cpp:186:        + request.selectedStream.behaviorHints.filename + QLatin1Char('|')
src\core\stream\SubtitlesAggregator.cpp:187:        + request.selectedStream.source.fileNameHint + QLatin1Char('|')
src\core\manga\NyaaRuntimeSource.cpp:187:QList<NyaaSourceCandidate> NyaaRuntimeSource::parseCandidates(const 
QByteArray& payload) const
src\core\manga\NyaaRuntimeSource.cpp:189:    QList<NyaaSourceCandidate> out;
src\core\manga\NyaaRuntimeSource.cpp:191:    NyaaSourceCandidate cur;
src\core\manga\NyaaRuntimeSource.cpp:204:                cur = NyaaSourceCandidate{};
src\core\manga\NyaaRuntimeSource.cpp:244:              [](const NyaaSourceCandidate& a, const NyaaSourceCandidate& b) {
src\core\manga\NyaaRuntimeSource.cpp:264:        const QList<NyaaSourceCandidate> parsed = 
parseCandidates(reply->readAll());
src\core\manga\NyaaRuntimeSource.cpp:265:        for (const NyaaSourceCandidate& cand : parsed) {
src\core\manga\NyaaRuntimeSource.cpp:284:    QList<NyaaSourceCandidate> results = it->results;
src\core\manga\NyaaRuntimeSource.cpp:289:              [](const NyaaSourceCandidate& a, const NyaaSourceCandidate& b) {
src\core\manga\NyaaRuntimeSource.h:17:// per NyaaSourceCandidate.
src\core\manga\NyaaRuntimeSource.h:18:struct NyaaSourceCandidate {
src\core\manga\NyaaRuntimeSource.h:46:    void searchSucceeded(int requestId, const 
QList<tankoban::manga::NyaaSourceCandidate>& results);
src\core\manga\NyaaRuntimeSource.h:57:        QList<NyaaSourceCandidate> results;
src\core\manga\NyaaRuntimeSource.h:65:    QList<NyaaSourceCandidate> parseCandidates(const QByteArray& payload) const;
src\core\manga\PremiumCatalog.cpp:76:    const auto hints = root.value(QStringLiteral("behaviorHints")).toObject();
src\core\manga\PremiumCatalog.cpp:77:    out.behaviorHintsP2P   = hints.value(QStringLiteral("p2p")).toBool(true);
src\core\manga\PremiumCatalog.cpp:78:    out.behaviorHintsAdult = hints.value(QStringLiteral("adult")).toBool(false);
src\core\manga\PremiumCatalogSchema.h:43:    bool    behaviorHintsP2P    = true;  // truthful: this is a P2P-backed 
source
src\core\manga\PremiumCatalogSchema.h:44:    bool    behaviorHintsAdult  = false; // never true for v1
src\core\manga\wikipedia\WikipediaResolver.cpp:24:QString urlPageEncode(const QString& displayTitle)
src\core\manga\wikipedia\WikipediaResolver.cpp:26:    QString s = displayTitle;

 succeeded in 3435ms:
src\core\stream\AutoSourcePicker.h:10:struct SourceCandidate {
src\core\stream\AutoSourcePicker.h:31:class AutoSourcePicker {
src\core\stream\AutoSourcePicker.h:34:    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
src\core\stream\AutoSourcePicker.h:42:    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
src\core\stream\AutoSourcePicker.h:49:    static bool   titleMatchesShow(const QString& candidateTitle,
src\core\stream\AutoSourcePicker.cpp:26:bool AutoSourcePicker::titleMatchesShow(const QString& candidateTitle,
src\core\stream\AutoSourcePicker.cpp:57:std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& 
candidates,
src\core\stream\AutoSourcePicker.cpp:59:    return pick(candidates, QString(), runtimeMinutes);  // no show gate
src\core\stream\AutoSourcePicker.cpp:62:std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& 
candidates,
src\core\stream\AutoSourcePicker.cpp:79:        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) 
continue;
src\ui\pages\StreamPage.cpp:3368:    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
src\ui\pages\StreamPage.cpp:3397:    // (what pick() now uses) vs the old displayTitle. If the pick still fails,
src\ui\pages\StreamPage.cpp:3404:                          << "gateBlob=" << 
tankostream::stream::AutoSourcePicker::titleMatchesShow(
src\ui\pages\StreamPage.cpp:3406:                          << "gateOld=" << 
tankostream::stream::AutoSourcePicker::titleMatchesShow(
src\ui\pages\StreamPage.cpp:3413:        tankostream::stream::AutoSourcePicker::pick(cands, ctx.showTitle, 
ctx.runtimeMinutes);
tests\core\stream\test_auto_source_picker.cpp:16:    auto idx = AutoSourcePicker::pick(v, 24);
tests\core\stream\test_auto_source_picker.cpp:26:    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:31:    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:42:    EXPECT_FALSE(AutoSourcePicker::pick(v, 120).has_value());
tests\core\stream\test_auto_source_picker.cpp:47:    auto idx = AutoSourcePicker::pick(v, 24);
tests\core\stream\test_auto_source_picker.cpp:54:    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:59:    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:64:    auto idx = AutoSourcePicker::pick(v, 24);
tests\core\stream\test_auto_source_picker.cpp:71:    auto idx = AutoSourcePicker::pick(v, 0);
tests\core\stream\test_auto_source_picker.cpp:81:    auto idx = AutoSourcePicker::pick(v, 24);
tests\core\stream\test_auto_source_picker.cpp:88:    EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());  // 
unknown size -> kept
tests\core\stream\test_auto_source_picker.cpp:92:    EXPECT_FALSE(AutoSourcePicker::pick({}, 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:98:// pick(candidates, showTitle, runtime) rejects candidates whose 
release title
tests\core\stream\test_auto_source_picker.cpp:106:    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24);
tests\core\stream\test_auto_source_picker.cpp:116:    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:120:    EXPECT_TRUE(AutoSourcePicker::titleMatchesShow(
tests\core\stream\test_auto_source_picker.cpp:122:    EXPECT_TRUE(AutoSourcePicker::titleMatchesShow(
tests\core\stream\test_auto_source_picker.cpp:124:    EXPECT_FALSE(AutoSourcePicker::titleMatchesShow(
tests\core\stream\test_auto_source_picker.cpp:126:    EXPECT_FALSE(AutoSourcePicker::titleMatchesShow(  // shares only 
the stop-ish 'one'
tests\core\stream\test_auto_source_picker.cpp:132:    EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());          
  // int overload
tests\core\stream\test_auto_source_picker.cpp:133:    EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 
24).has_value()); // empty gate
tests\core\stream\test_auto_source_picker.cpp:156:    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24);
tests\core\stream\test_auto_source_picker.cpp:169:    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:181:    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24);
tests\core\stream\test_auto_source_picker.cpp:191:    EXPECT_TRUE(AutoSourcePicker::pick(v, QStringLiteral("One 
Piece"), 24).has_value());
tests\core\stream\test_auto_source_picker.cpp:195:    EXPECT_FALSE(AutoSourcePicker::pick(w, QStringLiteral("One 
Piece"), 24).has_value());

codex
I found the patch applied in the working tree, and the only current `SourceCandidate` construction outside tests 
appears to be the `finishAutoDownloadPick` path. I’m checking the actual token matcher and stream data structures now, 
especially whether the blob fields are stable and whether scoring still uses the original title.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\core\\stream\\AutoSourcePicker.cpp | Select-Object -First 140" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\ui\\pages\\stream\\StreamSourceChoice.h | Select-Object -First 90" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\core\\stream\\addon\\StreamInfo.h | Select-Object -First 90" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\core\\stream\\AutoSourcePicker.h | Select-Object -First 100" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\core\\stream\\addon\\StreamSource.h | Select-Object -First 80" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 2806ms:
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include "StreamSource.h"
#include "SubtitleInfo.h"

namespace tankostream::addon {

struct StreamBehaviorHints {
    bool notWebReady = false;
    QString bingeGroup;
    QStringList countryWhitelist;

    QHash<QString, QString> proxyRequestHeaders;
    QHash<QString, QString> proxyResponseHeaders;

    QString filename;
    QString videoHash;
    qint64 videoSize = 0;

    QVariantMap other;
};

struct Stream {
    StreamSource source;
    QString name;
    QString description;
    QUrl thumbnail;
    StreamBehaviorHints behaviorHints;
    QList<SubtitleTrack> subtitles;
};

}

 succeeded in 3226ms:
#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace tankostream::addon {

struct StreamSource {
    enum class Kind {
        Url,
        Magnet,
        YouTube,
        Http,
    };

    Kind kind = Kind::Url;

    QUrl url;

    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
    QStringList trackers;

    QString youtubeId;

    static StreamSource urlSource(const QUrl& value)
    {
        StreamSource source;
        source.kind = Kind::Url;
        source.url = value;
        return source;
    }

    static StreamSource httpSource(const QUrl& value)
    {
        StreamSource source;
        source.kind = Kind::Http;
        source.url = value;
        return source;
    }

    static StreamSource magnetSource(const QString& hash,
                                     const QStringList& trackerList = {},
                                     int selectedFileIndex = -1,
                                     const QString& hint = {})
    {
        StreamSource source;
        source.kind = Kind::Magnet;
        source.infoHash = hash;
        source.trackers = trackerList;
        source.fileIndex = selectedFileIndex;
        source.fileNameHint = hint;
        return source;
    }

    static StreamSource youtubeSource(const QString& ytId)
    {
        StreamSource source;
        source.kind = Kind::YouTube;
        source.youtubeId = ytId;
        return source;
    }

    QString toMagnetUri() const
    {
        if (kind != Kind::Magnet || infoHash.isEmpty()) {
            return {};
        }
        QString uri = QStringLiteral("magnet:?xt=urn:btih:") + infoHash;
        for (const QString& tracker : trackers) {
            uri += QStringLiteral("&tr=") +
                   QString::fromUtf8(QUrl::toPercentEncoding(tracker));
        }
        return uri;
    }
};

}

 succeeded in 3288ms:
#pragma once
#include <QList>
#include <QString>
#include <optional>

namespace tankostream::stream {

// One Torrentio candidate reduced to the fields auto-pick needs.
// Pure data; no Qt-UI dependency (mirrors QualityScorer's layering).
struct SourceCandidate {
    QString title;            // release title (CAM detection + tiebreak)
    int     seeders = 0;
    qint64  sizeBytes = 0;
    int     qualitySort = 0;  // 5=2160p 4=1440p 3=1080p 2=720p 1=480p 0=unknown

    // DOWNLOAD BUG 2026-06-03 â€” text the show-identity gate matches against.
    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
    // anime-shaped Torrentio) put a stats badge ("ðŸ‘¤ 2176 ðŸ’¾ 1.34 GB âš™ï¸ NyaaSi")
    // there instead of the filename, so gating on `title` rejected EVERY real
    // One Piece result ("No 1080p source found"). matchText carries the full
    // raw identity blob (name + description + fileNameHint + â€¦) so the gate can
    // find the show name wherever the addon stashed it. Empty == fall back to
    // `title` (preserves every existing caller + test that only sets `title`).
    QString matchText;
};

// Silent best-source selection for Theatre's one-tap download.
// Filter (1080p / seeders>0 / not-CAM) -> rank by seeders -> size guardrail
// only for the weakly-seeded tail. Returns the index into `candidates` of
// the chosen source, or std::nullopt for "no source found".
class AutoSourcePicker {
public:
    // runtimeMinutes <= 0 means "unknown" -> size guardrail skipped.
    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
                                   int runtimeMinutes = 0);

    // Show-identity gate (DOWNLOAD BUG 2026-06-02): when showTitle is non-empty,
    // candidates whose release title lacks the show's significant tokens are
    // rejected BEFORE ranking â€” so a One Piece request can never download a
    // 'Community' pack just because it is better-seeded. Empty showTitle = no gate
    // (identical to the runtime-only overload above).
    static std::optional<int> pick(const QList<SourceCandidate>& candidates,
                                   const QString& showTitle,
                                   int runtimeMinutes = 0);

    // True iff every significant token of showTitle appears as a whole word in
    // candidateTitle (both normalized: lowercased, separators->space). Stopwords
    // and 1-char tokens are ignored; an all-stopword show title never blocks.
    static bool   titleMatchesShow(const QString& candidateTitle,
                                   const QString& showTitle);

    static bool   isCamRip(const QString& title);
    static double impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes);

    static constexpr int    kRequiredQualitySort = 3;     // 1080p only
    static constexpr int    kLowSeedThreshold     = 30;    // below = tail
    static constexpr double kMinBitrateMbps       = 1.5;   // re-encode floor
    static constexpr double kMaxBitrateMbps       = 20.0;  // remux/4K ceiling
};

}  // namespace tankostream::stream

 succeeded in 3511ms:
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/stream/addon/StreamInfo.h"

namespace tankostream::stream {

// Stream picker choice â€” the payload StreamPage consumes after the user
// selects a stream card. Previously owned by StreamPickerDialog; moved here
// when the modal dialog was replaced with the inline source panel so the
// struct survives the dialog's deletion and both the panel widgets and
// StreamPage can share one definition.
//
// Display fields (prefixed `display*` + `badges`/`sizeBytes`/`seeders`) are
// populated by `buildPickerChoices` and consumed by `StreamSourceCard`.
// StreamPage only cares about `stream` + `addonId` + `addonName` + the
// magnet-dispatch block; the display fields are there so the card doesn't
// have to reach back into `stream.behaviorHints.other` for everything.
struct StreamPickerChoice {
    tankostream::addon::Stream stream;
    QString addonId;
    QString addonName;
    QString sourceKind;        // "magnet" / "http" / "url" / "youtube"

    QString magnetUri;
    QString infoHash;
    int     fileIndex = -1;
    QString fileNameHint;

    // â”€â”€ UI display fields â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 â€” displayTitle semantic
    // changed: WAS the addon name (Stremio shows release name primary,
    // we were showing "Torrentio" on every card â€” useless as
    // disambiguation when most results come from one addon). Now carries
    // the extracted release name (e.g. "The.Boys.S03E04.1080p..."); the
    // addon name moves to the card's footer line via addonName above.
    // For direct streams with no resolvable release name, falls back to
    // "Direct stream" so the row still reads.
    QString     displayTitle;     // primary line â€” release name (addon-agnostic)
    QString     displayQuality;   // right-aligned pill â€” "1080p", "4K HDR", "-"
    qint64      sizeBytes = 0;    // raw; card formats via humanSize
    int         seeders   = 0;    // magnet only; -1 for non-magnet (rendered as "-")
    QStringList badges;           // ["HDR"], ["DV"], ["MULTI-SUB"], etc.
    QString     trackerSource;    // small-caps footer hint when populated
    bool        isDirect  = false; // true for HTTP/URL direct streams
    int         qualitySort = 0;   // 5=2160p, 4=1440p, 3=1080p, 2=720p, 1=480p, 0=unknown

    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 â€” release-shape chip data,
    // Stremio parity. Surfaced inline on the chip row (between title and
    // footer) so a glance at a card answers "single episode? season pack?
    // full series?" without parsing the release name. Empty when no
    // pattern matches (e.g. movies â€” no S/E shape to detect, no chip
    // shown). detectPackType is the populator; see StreamSourceChoice.cpp.
    QString     packType;         // "" | "episode" | "season" | "series"
    QString     packLabel;        // "" | "S03E04" | "Season 3" | "Complete Series"
};

// Build the full sorted picker-choice list from aggregator output.
// Sort order: magnets-with-seeders first (by seeder count desc), then by
// quality desc, then by size desc, then by title asc â€” matches the legacy
// StreamPickerDialog ordering so user expectations are preserved.
QList<StreamPickerChoice> buildPickerChoices(
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>&           addonsById);

// Human-readable size (e.g. "3.9 GB"). Exposed so both the card and any
// future list-header summary can share one formatter.
QString humanSize(qint64 bytes);

// Build a canonical StreamChoice-JSON-compatible key for matching a
// saved-choice record back to its picker card. Mirrors the shape used in
// StreamPage.cpp when saving (addonId + sourceKind + infoHash/url + fileIndex).
QString pickerChoiceKey(const StreamPickerChoice& choice);

}

 succeeded in 3778ms:
#include "core/stream/AutoSourcePicker.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include "core/stream/QualityScorer.h"

namespace tankostream::stream {

bool AutoSourcePicker::isCamRip(const QString& title) {
    // Camcorder-class tags, bounded so ordinary words don't match.
    // "TS"/"TC" intentionally NOT matched (false positives like "GUTS");
    // can be added with care later (spec section 8.2).
    static const QRegularExpression re(
        QStringLiteral("\\b(cam|camrip|hdcam|telesync|telecine|hdts)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(title).hasMatch();
}

double AutoSourcePicker::impliedBitrateMbps(qint64 sizeBytes, int runtimeMinutes) {
    if (sizeBytes <= 0 || runtimeMinutes <= 0) return 0.0;
    return (static_cast<double>(sizeBytes) * 8.0)
         / (static_cast<double>(runtimeMinutes) * 60.0) / 1.0e6;
}

bool AutoSourcePicker::titleMatchesShow(const QString& candidateTitle,
                                        const QString& showTitle) {
    auto norm = [](const QString& s) {
        static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
        QString t = s.toLower();
        t.replace(nonAlnum, QStringLiteral(" "));
        return t.simplified();
    };
    const QString show = norm(showTitle);
    if (show.isEmpty()) return true;  // nothing to gate on

    static const QSet<QString> kStop = {
        QStringLiteral("the"),    QStringLiteral("a"),      QStringLiteral("an"),
        QStringLiteral("of"),     QStringLiteral("and"),    QStringLiteral("to"),
        QStringLiteral("in"),     QStringLiteral("season"), QStringLiteral("series")};

    const QStringList candWords =
        norm(candidateTitle).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QSet<QString> candSet(candWords.begin(), candWords.end());

    int significant = 0;
    int matched = 0;
    for (const QString& w : show.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (w.size() < 2 || kStop.contains(w)) continue;  // skip noise tokens
        ++significant;
        if (candSet.contains(w)) ++matched;
    }
    if (significant == 0) return true;     // show title was all stopwords/short
    return matched == significant;          // every significant show token present
}

std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidates,
                                          int runtimeMinutes) {
    return pick(candidates, QString(), runtimeMinutes);  // no show gate
}

std::optional<int> AutoSourcePicker::pick(const QList<SourceCandidate>& candidates,
                                          const QString& showTitle,
                                          int runtimeMinutes) {
    // Step 1 - hard filters.
    QList<int> survivors;
    for (int i = 0; i < candidates.size(); ++i) {
        const SourceCandidate& cand = candidates.at(i);
        if (cand.qualitySort != kRequiredQualitySort) continue;  // 1080p only
        if (cand.seeders <= 0) continue;                          // dead torrent
        if (isCamRip(cand.title)) continue;                       // camcorder rip
        // Show-identity gate: never pick a release that isn't the requested
        // show, no matter how well-seeded (the One Piece -> Community bug).
        // DOWNLOAD BUG 2026-06-03 â€” gate on matchText (full raw identity blob)
        // when present; some addons put a stats badge in `title`, which lacks
        // the show name and rejected every real result. Falls back to `title`
        // so callers/tests that only set `title` are unaffected.
        const QString& idText = cand.matchText.isEmpty() ? cand.title : cand.matchText;
        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) continue;
        survivors.append(i);
    }
    if (survivors.isEmpty()) return std::nullopt;                 // no source found

    // Rank: seeders desc -> release-type (sourceScore) desc -> lower original
    // index. Deterministic even when seeders tie.
    auto betterRank = [&](int a, int b) {
        const SourceCandidate& ca = candidates.at(a);
        const SourceCandidate& cb = candidates.at(b);
        if (ca.seeders != cb.seeders) return ca.seeders > cb.seeders;
        const int sa = QualityScorer::sourceScore(ca.title);
        const int sb = QualityScorer::sourceScore(cb.title);
        if (sa != sb) return sa > sb;
        return a < b;
    };
    std::sort(survivors.begin(), survivors.end(), betterRank);

    // Step 2 - well-seeded survivor decides outright; size never consulted.
    if (candidates.at(survivors.first()).seeders >= kLowSeedThreshold)
        return survivors.first();

    // Step 3 - weakly-seeded tail. Runtime unknown -> cannot apply the size
    // guardrail; return the best-ranked survivor.
    if (runtimeMinutes <= 0)
        return survivors.first();

    // Drop implausible implied-bitrate releases, then take the best-ranked of
    // the rest. A candidate with unknown size (sizeBytes <= 0 -> 0.0) is kept.
    QList<int> sane;
    for (int idx : survivors) {
        const double mbps = impliedBitrateMbps(candidates.at(idx).sizeBytes, runtimeMinutes);
        if (mbps <= 0.0) { sane.append(idx); continue; }          // size unknown -> keep
        if (mbps < kMinBitrateMbps || mbps > kMaxBitrateMbps) continue;  // junk
        sane.append(idx);
    }
    if (sane.isEmpty()) return std::nullopt;
    std::sort(sane.begin(), sane.end(), betterRank);
    return sane.first();
}

}  // namespace tankostream::stream

codex
The matcher itself is strict in the narrow sense: it normalizes all non-alphanumerics to spaces and requires every 
non-stopword show token as a whole word. That means the newline join is harmless, but it also means any included blob 
field that contains all significant show tokens will satisfy the gate, regardless of which field supplied them.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
tests\\core\\stream\\test_auto_source_picker.cpp | Select-Object -First 220" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\ui\\pages\\StreamPage.cpp | Select-Object -Skip 3340 -First 95" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\ui\\pages\\stream\\StreamSourceChoice.cpp | Select-Object -Skip 100 -First 280" in 
C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "Get-Content -Path 
src\\core\\stream\\StreamAggregator.cpp | Select-Object -Skip 320 -First 150" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 2334ms:
            fileIdx);
        return true;
    }

    const QString ytId = streamObj.value(QStringLiteral("ytId")).toString().trimmed();
    if (!ytId.isEmpty()) {
        sourceOut = StreamSource::youtubeSource(ytId);
        return true;
    }

    QString urlValue = streamObj.value(QStringLiteral("url")).toString().trimmed();
    if (urlValue.isEmpty()) {
        urlValue = streamObj.value(QStringLiteral("externalUrl")).toString().trimmed();
    }
    if (urlValue.isEmpty()) {
        urlValue = streamObj.value(QStringLiteral("playerFrameUrl")).toString().trimmed();
    }
    const QUrl url(urlValue);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        sourceOut = StreamSource::httpSource(url);
    } else {
        sourceOut = StreamSource::urlSource(url);
    }
    return true;
}

void enrichTorrentioLikeFields(Stream& stream, const QJsonObject& streamObj)
{
    if (stream.source.kind != StreamSource::Kind::Magnet) {
        return;
    }
    if (stream.behaviorHints.bingeGroup.isEmpty()) {
        return;
    }

    const QString rawTitle = streamObj.value(QStringLiteral("title")).toString();
    if (rawTitle.isEmpty()) {
        return;
    }

    const QStringList lines = rawTitle.split('\n');
    QString trackerSource;
    QString tracker;
    QString languages;
    QString fileHintFromTitle;
    int seeders = 0;
    qint64 sizeBytes = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (i == 0) {
            trackerSource = line;
            continue;
        }
        if (containsBust(line)) {
            const auto m = kSeedersRe.match(line);
            if (m.hasMatch()) {
                seeders = m.captured(1).toInt();
            }
        }
        if (containsDisk(line)) {
            const auto m = kSizeRe.match(line);
            if (m.hasMatch()) {
                sizeBytes = parseSize(m.captured(1));
            }
        }
        if (containsGear(line)) {
            const auto m = kTrackerRe.match(line);
            if (m.hasMatch()) {
                tracker = m.captured(1).trimmed();
            }
        }
        if (containsFlag(line)) {
            languages = line;
        }
        if (fileHintFromTitle.isEmpty() && looksLikeFilename(line)) {
            fileHintFromTitle = extractFilename(line);
        }
    }

    const QString quality = parseQuality(rawTitle);
    if (!quality.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("qualityLabel"), quality);
    }
    if (!trackerSource.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("trackerSource"), trackerSource);
    }
    if (!tracker.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("tracker"), tracker);
    }
    if (!languages.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("languages"), languages);
    }
    if (seeders > 0) {
        stream.behaviorHints.other.insert(QStringLiteral("seeders"), seeders);
    }
    if (sizeBytes > 0) {
        stream.behaviorHints.other.insert(QStringLiteral("sizeBytes"),
                                          QVariant::fromValue<qint64>(sizeBytes));
    }

    if (!fileHintFromTitle.isEmpty() && stream.source.fileNameHint.isEmpty()) {
        stream.source.fileNameHint = fileHintFromTitle;
    }
    if (!stream.behaviorHints.filename.isEmpty()) {
        stream.source.fileNameHint = stream.behaviorHints.filename;
    }

    // Stream-picker UX rework â€” preserve the parsed filename as a UI-readable
    // field. Stremio-style source cards render this as the primary line under
    // the addon name; without it the table showed "Torrentio..." for every
    // Torrentio row because neither stream.name nor stream.description carry
    // anything useful per Torrentio payload. Mirror into behaviorHints.filename
    // when empty so Stremio-compliant addons that populate `filename` directly
    // and Torrentio-style addons that embed it in `title` look identical to
    // the card layer.
    if (!fileHintFromTitle.isEmpty()) {
        stream.behaviorHints.other.insert(QStringLiteral("parsedFilename"),
                                          fileHintFromTitle);
        if (stream.behaviorHints.filename.isEmpty()) {
            stream.behaviorHints.filename = fileHintFromTitle;
        }
    }

    if (stream.source.trackers.isEmpty()) {
        stream.source.trackers = kFallbackTrackers.mid(0, kMaxTrackers);
    }
}

QString streamIdentityKey(const Stream& stream)
{
    switch (stream.source.kind) {
    case StreamSource::Kind::Magnet:
        return QStringLiteral("magnet|%1|%2|%3")
            .arg(stream.source.infoHash.toLower())
            .arg(stream.source.fileIndex)
            .arg(stream.source.fileNameHint);
    case StreamSource::Kind::Http:
        return QStringLiteral("http|%1").arg(stream.source.url.toString(QUrl::FullyEncoded));
    case StreamSource::Kind::Url:
        return QStringLiteral("url|%1").arg(stream.source.url.toString(QUrl::FullyEncoded));
    case StreamSource::Kind::YouTube:

 succeeded in 2608ms:

    // DOWNLOAD BUG 2026-06-02 — capture the generation token load() stamped so
    // the one-shot above can discard a late emit from a superseded load().
    // Set AFTER m_pendingAuto fields are populated.
    m_pendingAuto.token = m_streamAggregator->load(req);
}

// One-shot streamsReady handler for an active auto-download. Converts the
// Torrentio results into AutoSourcePicker candidates, picks the best 1080p,
// resolves the infoHash, and starts the download stamped theatre:<imdbId>.
void StreamPage::finishAutoDownloadPick(const QList<tankostream::addon::Stream>& streams,
                                        const QHash<QString, QString>& addonsById)
{
    if (!m_pendingAuto.active) return;
    const PendingAutoDownload ctx = m_pendingAuto;
    m_pendingAuto.active = false;  // consume

    const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);
    qInfo().noquote() << "[auto-dl] finishPick choices=" << choices.size()
                      << "active=" << (m_pendingAuto.active ? "y" : "n");

    // DOWNLOAD BUG 2026-06-03 — build the show-identity match text from the RAW
    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
    // put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi") in displayTitle, which
    // lacks the show name and made the gate reject every real One Piece result
    // ("No 1080p source found"). The raw blob carries the filename wherever the
    // addon stashed it (name / description / fileNameHint / parsedFilename), so
    // titleMatchesShow can find "One Piece". displayTitle stays the CAM/tiebreak
    // title (its job in the picker is unchanged).
    auto identityBlob = [](const tankostream::stream::StreamPickerChoice& ch) {
        return QStringList{
            ch.displayTitle,
            ch.stream.name,
            ch.stream.description,
            ch.stream.source.fileNameHint,
            ch.fileNameHint,
            ch.stream.behaviorHints.filename,
            ch.stream.behaviorHints.other
                .value(QStringLiteral("parsedFilename")).toString(),
        }.join(QLatin1Char('\n'));
    };

    QList<tankostream::stream::SourceCandidate> cands;
    cands.reserve(choices.size());
    for (const auto& c : choices) {
        tankostream::stream::SourceCandidate sc;
        sc.title       = c.displayTitle;
        sc.matchText    = identityBlob(c);
        sc.seeders     = c.seeders;
        sc.sizeBytes   = c.sizeBytes;
        sc.qualitySort = c.qualitySort;
        cands.append(sc);
    }

    // DOWNLOAD BUG 2026-06-02/06-03 — diagnostic: dump every candidate with
    // whether it passes the show-identity gate against the RAW identity blob
    // (what pick() now uses) vs the old displayTitle. If the pick still fails,
    // this shows conclusively whether real One Piece titles are being wrongly
    // rejected (gate too strict / blob missing the name) vs the choices being
    // the wrong show entirely (correlation broken — wrong streams reached here).
    for (const auto& ch : choices) {
        const QString blob = identityBlob(ch);
        qInfo().noquote() << "[auto-dl] cand q=" << ch.qualitySort << "seed=" << ch.seeders
                          << "gateBlob=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
                                            blob, ctx.showTitle)
                          << "gateOld=" << tankostream::stream::AutoSourcePicker::titleMatchesShow(
                                            ch.displayTitle, ctx.showTitle)
                          << "title=" << ch.displayTitle.left(60)
                          << "| blob=" << blob.simplified().left(100);
    }

    const std::optional<int> picked =
        tankostream::stream::AutoSourcePicker::pick(cands, ctx.showTitle, ctx.runtimeMinutes);
    qInfo().noquote() << "[auto-dl] picked="
                      << (picked.has_value() ? QString::number(*picked) : QStringLiteral("NONE"))
                      << "of" << cands.size();
    if (!picked.has_value()) {
        // No acceptable 1080p source. (P1.T4 refines this into a tile state;
        // for now surface it in the sources panel.)
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("No 1080p source found"));
        return;
    }

    const tankostream::stream::StreamPickerChoice& chosen = choices.at(*picked);

    QString hash = chosen.infoHash;
    if (hash.isEmpty() && !chosen.magnetUri.isEmpty())
        hash = m_torrentClient->resolveMetadata(chosen.magnetUri);
    if (hash.isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("Could not resolve source"));
        return;
    }


 succeeded in 2707ms:
#include <gtest/gtest.h>
#include "core/stream/AutoSourcePicker.h"

using tankostream::stream::AutoSourcePicker;
using tankostream::stream::SourceCandidate;

static SourceCandidate c(const QString& title, int seeders, qint64 sizeBytes, int qualitySort) {
    SourceCandidate s; s.title = title; s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; 
return s;
}

TEST(AutoSourcePicker, PicksHighestSeeded1080p) {
    QList<SourceCandidate> v {
        c("One Piece S01E01 [SubsPlease] 1080p", 1200, 1400000000LL, 3),
        c("One Piece S01E01 1080p WEB-DL",          300, 1500000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, ExcludesNon1080p) {
    QList<SourceCandidate> v {
        c("Show S01E01 720p",  900, 700000000LL, 2),
        c("Show S01E01 2160p", 800, 9000000000LL, 5),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesZeroSeeders) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 0, 1400000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesCamRips) {
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie 2024 1080p CAM"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.TELESYNC.1080p"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.HDCAM.1080p"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("Movie.2024.1080p.WEB-DL"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("GUTS.S01E01.1080p"));  // no false-positive on 'TS'

    QList<SourceCandidate> v { c("Movie 2024 1080p CAM", 500, 3000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 120).has_value());
}

TEST(AutoSourcePicker, WellSeededIgnoresSize) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 1200, 30000000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, LowSeedTailDropsReencode) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 4, 180000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailDropsRemux) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 6, 13000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailKeepsSaneSized) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 1400000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, UnknownRuntimeSkipsSizeGuardrail) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 180000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 0);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, TieOnSeedersBreaksByReleaseType) {
    QList<SourceCandidate> v {
        c("Show S01E01 1080p WEBRip", 10, 1400000000LL, 3),
        c("Show S01E01 1080p BluRay", 10, 1400000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1);  // BluRay (higher sourceScore) wins the tie
}

TEST(AutoSourcePicker, NegativeSizeTreatedAsUnknown) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, -1LL, 3) };
    EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());  // unknown size -> kept
}

TEST(AutoSourcePicker, EmptyListReturnsNone) {
    EXPECT_FALSE(AutoSourcePicker::pick({}, 24).has_value());
}

// â”€â”€ Show-identity gate (DOWNLOAD BUG 2026-06-02) 
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Clicking Download on One Piece downloaded 'Community' because the picker
// ranked by seeders/quality and never checked the result was the right show.
// pick(candidates, showTitle, runtime) rejects candidates whose release title
// lacks the show's significant tokens BEFORE ranking.

TEST(AutoSourcePicker, TitleGateSkipsWrongShowEvenIfBetterSeeded) {
    QList<SourceCandidate> v {
        c("Community.S01-S06.COMPLETE.1080p.BluRay.x265-POIASD", 5000, 50000000000LL, 3),
        c("One Piece - 1164 [1080p].mkv",                         800,  1400000000LL,  3),
    };
    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1) << "must skip the better-seeded Community pack and pick One Piece";
}

TEST(AutoSourcePicker, TitleGateReturnsNoneWhenNoCandidateMatchesShow) {
    QList<SourceCandidate> v {
        c("Community.S01-S06.COMPLETE.1080p.BluRay.x265", 5000, 50000000000LL, 3),
        c("Friends.S01.1080p.BluRay.x265",                4000, 40000000000LL, 3),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
}

TEST(AutoSourcePicker, TitleMatchesShowNormalizesSeparatorsAndTokens) {
    EXPECT_TRUE(AutoSourcePicker::titleMatchesShow(
        QStringLiteral("One.Piece.1164.1080p.WEB-DL"), QStringLiteral("One Piece")));
    EXPECT_TRUE(AutoSourcePicker::titleMatchesShow(
        QStringLiteral("[Erai-raws] One Piece - 1164 [1080p]"), QStringLiteral("One Piece")));
    EXPECT_FALSE(AutoSourcePicker::titleMatchesShow(
        QStringLiteral("Community.S01-S06.COMPLETE.1080p"), QStringLiteral("One Piece")));
    EXPECT_FALSE(AutoSourcePicker::titleMatchesShow(  // shares only the stop-ish 'one'
        QStringLiteral("One.Tree.Hill.S01.1080p"), QStringLiteral("One Piece")));
}

TEST(AutoSourcePicker, EmptyShowTitleSkipsGate) {
    QList<SourceCandidate> v { c("Anything 1080p", 1000, 1400000000LL, 3) };
    EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());            // int overload
    EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 24).has_value()); // empty gate
}

// â”€â”€ matchText identity blob (DOWNLOAD BUG 2026-06-03) 
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// NyaaSi / anime-shaped addons put a stats badge ("ðŸ‘¤ 2176 ðŸ’¾ 1.34 GB âš™ï¸ NyaaSi")
// in the display title, so gating on `title` rejected EVERY real One Piece
// result -> "No 1080p source found" even though the right show's sources
// arrived. The gate now matches `matchText` (full raw identity blob) when set,
// falling back to `title` when empty.

static SourceCandidate cm(const QString& title, const QString& matchText,
                          int seeders, qint64 sizeBytes, int qualitySort) {
    SourceCandidate s; s.title = title; s.matchText = matchText;
    s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
}

TEST(AutoSourcePicker, MatchTextLetsBadgeTitledOnePiecePassGate) {
    // title is the useless stats badge; the real filename lives in matchText.
    QList<SourceCandidate> v {
        cm(QStringLiteral("ðŸ‘¤ 2176 ðŸ’¾ 1.34 GB âš™ï¸ NyaaSi"),
           QStringLiteral("ðŸ‘¤ 2176 ðŸ’¾ 1.34 GB âš™ï¸ NyaaSi\n[SubsPlease] One Piece - 1164 (1080p) 
[F9D2E8C1].mkv"),
           2176, 1400000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
    ASSERT_TRUE(idx.has_value()) << "matchText carries 'One Piece' -> must pass the gate";
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, MatchTextStillRejectsWrongShowBadge) {
    // Badge title AND a matchText for the wrong show -> still rejected (the gate
    // is not weakened, it just looks at the right text).
    QList<SourceCandidate> v {
        cm(QStringLiteral("ðŸ‘¤ 5000 ðŸ’¾ 50 GB âš™ï¸ NyaaSi"),
           QStringLiteral("ðŸ‘¤ 5000 ðŸ’¾ 50 GB âš™ï¸ NyaaSi\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
           5000, 50000000000LL, 3),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
}

TEST(AutoSourcePicker, MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
    QList<SourceCandidate> v {
        cm(QStringLiteral("ðŸ‘¤ 5000 ðŸ’¾ 50 GB âš™ï¸ NyaaSi"),
           QStringLiteral("ðŸ‘¤ 5000\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
           5000, 50000000000LL, 3),
        cm(QStringLiteral("ðŸ‘¤ 800 ðŸ’¾ 1.34 GB âš™ï¸ NyaaSi"),
           QStringLiteral("ðŸ‘¤ 800\n[Erai-raws] One Piece - 1164 [1080p].mkv"),
           800, 1400000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1) << "must skip the better-seeded wrong-show badge and pick One Piece";
}

TEST(AutoSourcePicker, EmptyMatchTextFallsBackToTitleGate) {
    // matchText empty -> gate uses title (unchanged legacy behavior).
    QList<SourceCandidate> v {
        c(QStringLiteral("One Piece - 1164 [1080p].mkv"), 800, 1400000000LL, 3),
    };
    EXPECT_TRUE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
    QList<SourceCandidate> w {
        c(QStringLiteral("Community.S01.1080p.mkv"), 800, 1400000000LL, 3),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(w, QStringLiteral("One Piece"), 24).has_value());
}

 succeeded in 2855ms:
    QString uri = QStringLiteral("magnet:?xt=urn:btih:") + stream.source.infoHash.toLower();
    for (const QString& tracker : stream.source.trackers) {
        uri += QStringLiteral("&tr=")
             + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
    }
    return uri;
}

// STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 â€” replaced bestFilename with
// stricter extractReleaseName contract: returns ONE LINE suitable for the
// card's primary identifier label (Stremio parity).
//
// Priority (addon-agnostic â€” Torrentio, Comet, MediaFusion, Jackett-relayed
// addons all emit roughly this shape, with the exact field varying per
// addon-version):
//   1. behaviorHints.other["parsedFilename"] â€” Torrentio-style enrichment;
//      already a clean single line.
//   2. source.fileNameHint â€” Stremio spec field; already clean.
//   3. behaviorHints.filename â€” Stremio spec field, alternate carrier.
//   4. stream.name â€” addon-supplied; reject if it's just an addon brand
//      tag with optional resolution echo ("Torrentio", "Torrentio 1080p")
//      since that's the failure mode we're fixing. Otherwise take
//      first line.
//   5. stream.description first line â€” Torrentio packs the release name
//      on line 1 with size/seeders/source on subsequent lines marked by
//      glyphs (ðŸ’¾ 4.2 GB, ðŸ‘¤ 152, etc). Reject the first line if it's
//      ITSELF a metadata row (starts with a glyph or "Size:"/"Seeders:").
//   6. "(unnamed release)" â€” distinct from the legacy "(untitled stream)"
//      so smoke can grep for the new failure mode separately.
QString extractReleaseName(const Stream& stream)
{
    auto firstLine = [](const QString& s) -> QString {
        const int nl = s.indexOf(QLatin1Char('\n'));
        return (nl < 0 ? s : s.left(nl)).trimmed();
    };

    const QString parsed = stream.behaviorHints.other
                               .value(QStringLiteral("parsedFilename")).toString().trimmed();
    if (!parsed.isEmpty()) return firstLine(parsed);

    if (!stream.source.fileNameHint.isEmpty())
        return firstLine(stream.source.fileNameHint);
    if (!stream.behaviorHints.filename.isEmpty())
        return firstLine(stream.behaviorHints.filename);

    // stream.name â€” reject if it's just the addon brand. The pattern
    // captures "Torrentio", "Comet", "MediaFusion", "Cinemeta" optionally
    // followed by a resolution token ("Torrentio 1080p"). Anchored so
    // longer names like "Torrentio Plus +" still pass through.
    if (!stream.name.isEmpty()) {
        static const QRegularExpression kAddonBrandOnly(
            QStringLiteral("^(torrentio|comet|mediafusion|cinemeta|opensubtitles)"
                           "\\s*(2160p|1440p|1080p|720p|480p|4k)?\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const QString candidate = firstLine(stream.name);
        if (!kAddonBrandOnly.match(candidate).hasMatch()) {
            return candidate;
        }
    }

    // stream.description â€” first line, but reject if it's a metadata row.
    if (!stream.description.isEmpty()) {
        const QString line0 = firstLine(stream.description);
        // Heuristic for metadata-only first lines: starts with a known
        // metadata glyph (Stremio-style ðŸ’¾ â‰ˆ U+1F4BE, ðŸ‘¤ â‰ˆ U+1F464) or
        // a "Size:"/"Seeders:" prefix, OR is purely numeric/sizing data.
        static const QRegularExpression kMetadataRow(
            QStringLiteral("^(\\p{So}|\\p{Sc}|\\p{Cs}|size:|seeders:|seeds:|peers:|"
                           "\\d+(\\.\\d+)?\\s*(b|kb|mb|gb|tb)\\b)"),
            QRegularExpression::CaseInsensitiveOption);
        if (!line0.isEmpty() && !kMetadataRow.match(line0).hasMatch()) {
            return line0;
        }
        // First line was metadata â€” try the second line. Torrentio
        // sometimes flips order across addon versions (release name on
        // line 2, metadata on line 1).
        const int firstNl = stream.description.indexOf(QLatin1Char('\n'));
        if (firstNl > 0 && firstNl + 1 < stream.description.size()) {
            const QString line1 = firstLine(stream.description.mid(firstNl + 1));
            if (!line1.isEmpty() && !kMetadataRow.match(line1).hasMatch()) {
                return line1;
            }
        }
    }

    return QStringLiteral("(unnamed release)");
}

// STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 â€” release-shape detector.
// Walks the release name + (as a fallback) the full description blob for
// episode/season/series patterns. Returns {packType, packLabel}; both
// empty on no match (movies, ad-hoc HTTP streams, anything without a
// recognizable shape token).
//
// Priority: episode > season > series. A season pack that name-drops one
// of its included episodes ("Season 3 (incl S03E04)") will trip the
// episode detector first â€” that's fine, Stremio behaves the same way.
QPair<QString, QString> detectPackType(const QString& releaseName,
                                        const Stream& stream)
{
    // Search both the release name AND the full description blob â€” some
    // addons stash the shape token only in description (not in the name).
    const QString haystack = releaseName + QLatin1Char('\n') + stream.description
                           + QLatin1Char('\n') + stream.name;

    // 1) Single episode â€” S03E04, S3E4, 3x04, season 3 episode 4.
    static const QRegularExpression kEpisodeSE(
        QStringLiteral("\\bS(\\d{1,2})\\s*[xE]\\s*(\\d{1,3})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kEpisodeXForm(
        QStringLiteral("\\b(\\d{1,2})x(\\d{1,3})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kEpisodeSE.match(haystack); m.hasMatch()) {
        return { QStringLiteral("episode"),
                 QStringLiteral("S%1E%2")
                     .arg(m.captured(1).toInt(), 2, 10, QChar('0'))
                     .arg(m.captured(2).toInt(), 2, 10, QChar('0')) };
    }
    if (auto m = kEpisodeXForm.match(haystack); m.hasMatch()) {
        return { QStringLiteral("episode"),
                 QStringLiteral("S%1E%2")
                     .arg(m.captured(1).toInt(), 2, 10, QChar('0'))
                     .arg(m.captured(2).toInt(), 2, 10, QChar('0')) };
    }

    // 2) Multi-season pack â€” "S01-S05", "Season 1-5", "Seasons 1 to 5".
    //    Detected before single-season because "Season 1-5" contains
    //    "Season 1" as a substring.
    static const QRegularExpression kMultiSeason(
        QStringLiteral("\\b(?:S(\\d{1,2})\\s*-\\s*S(\\d{1,2})|"
                       "Seasons?\\s*(\\d{1,2})\\s*(?:-|to)\\s*(\\d{1,2}))\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kMultiSeason.match(haystack); m.hasMatch()) {
        return { QStringLiteral("series"),
                 QStringLiteral("Complete Series") };
    }

    // 3) Complete-series free-text markers.
    static const QRegularExpression kCompleteSeries(
        QStringLiteral("\\b(complete\\s+(series|show|collection)|all\\s+seasons)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (kCompleteSeries.match(haystack).hasMatch()) {
        return { QStringLiteral("series"),
                 QStringLiteral("Complete Series") };
    }

    // 4) Single season â€” "Season 3", "S03 (without episode)", "Complete Season 3".
    static const QRegularExpression kSeasonWord(
        QStringLiteral("\\b(?:complete\\s+)?Seasons?\\s*(\\d{1,2})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kSeasonWord.match(haystack); m.hasMatch()) {
        return { QStringLiteral("season"),
                 QStringLiteral("Season %1").arg(m.captured(1).toInt()) };
    }
    // "S03" alone (no episode token following) â€” but be careful: "S03E04"
    // would have matched kEpisodeSE above, so by the time we reach here
    // a bare S\d+ implies a season pack. Use a negative-lookahead to
    // ensure we don't grab the S of an unrecognized SXX*EXX form.
    static const QRegularExpression kSeasonShort(
        QStringLiteral("\\bS(\\d{1,2})(?![\\dxE])\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = kSeasonShort.match(haystack); m.hasMatch()) {
        return { QStringLiteral("season"),
                 QStringLiteral("Season %1").arg(m.captured(1).toInt()) };
    }

    // No shape detected â€” movie, ad-hoc HTTP stream, or unparseable.
    return { QString(), QString() };
}

// Resolve the best addon label. Aggregator threads addon metadata through
// behaviorHints.other["originAddonId"/"originAddonName"]; fall back to the
// caller-provided addonsById map, then to a generic "Unknown addon" string.
void resolveAddonLabel(const Stream& stream,
                       const QHash<QString, QString>& addonsById,
                       QString& outId,
                       QString& outName)
{
    outId = stream.behaviorHints.other
                .value(QStringLiteral("originAddonId")).toString().trimmed();
    outName = stream.behaviorHints.other
                  .value(QStringLiteral("originAddonName")).toString().trimmed();
    if (outName.isEmpty() && addonsById.contains(outId)) {
        outName = addonsById.value(outId);
    }
    if (outName.isEmpty()) {
        outName = QStringLiteral("Unknown addon");
    }
}

}

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("-");
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && i < 4) {
        val /= 1024.0;
        ++i;
    }
    return QString::number(val, 'f', i > 0 ? 1 : 0) + QLatin1Char(' ') + units[i];
}

QString pickerChoiceKey(const StreamPickerChoice& choice)
{
    return choice.addonId + QLatin1Char('|')
         + choice.sourceKind + QLatin1Char('|')
         + (choice.sourceKind == QLatin1String("magnet")
                ? choice.infoHash.toLower()
                : choice.stream.source.url.toString(QUrl::FullyEncoded))
         + QLatin1Char('|')
         + QString::number(choice.fileIndex);
}

QList<StreamPickerChoice> buildPickerChoices(
    const QList<Stream>&            streams,
    const QHash<QString, QString>&  addonsById)
{
    QList<StreamPickerChoice> out;
    out.reserve(streams.size());

    for (const Stream& stream : streams) {
        StreamPickerChoice c;
        c.stream      = stream;
        c.sourceKind  = prettyKind(stream.source.kind);
        c.infoHash    = stream.source.infoHash;
        c.fileIndex   = stream.source.fileIndex;
        c.fileNameHint = stream.source.fileNameHint;
        c.magnetUri   = buildMagnetUri(stream);
        c.isDirect    = (stream.source.kind == StreamSource::Kind::Http
                      || stream.source.kind == StreamSource::Kind::Url);

        resolveAddonLabel(stream, addonsById, c.addonId, c.addonName);

        // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 â€” primary line is now
        // the release name (Stremio parity); addon name moves to the
        // card footer line. Direct streams without a resolvable release
        // name fall back to "Direct stream" so the row still reads.
        const QString release = extractReleaseName(stream);
        if (c.isDirect && release == QLatin1String("(unnamed release)")) {
            c.displayTitle = QStringLiteral("Direct stream");
        } else {
            c.displayTitle = release;
        }
        const auto pack   = detectPackType(release, stream);
        c.packType        = pack.first;
        c.packLabel       = pack.second;
        c.displayQuality  = extractQuality(stream);
        c.sizeBytes       = extractSizeBytes(stream);
        c.seeders         = (stream.source.kind == StreamSource::Kind::Magnet)
                                ? extractSeeders(stream)
                                : -1;
        c.badges          = extractBadges(stream, c.displayQuality);
        c.trackerSource   = stream.behaviorHints.other
                                .value(QStringLiteral("trackerSource")).toString().trimmed();
        c.qualitySort     = qualityRank(c.displayQuality);

        out.push_back(c);
    }

    // STREAM_HTTP_PREFER Phase 0.1 â€” rank direct HTTP/Url sources ABOVE
    // magnet sources in the stream picker so debrid-backed CDN links land
    // first. Cold-open on a libtorrent magnet can take minutes on a fresh
    // swarm (piece scheduler starvation despite healthy peer + bandwidth
    // state â€” see STREAM_HTTP_PREFER_FIX_TODO.md "Created" block for the
    // log evidence); the same title's HTTP stream via Real-Debrid /
    // Premiumize / etc is CDN-bound and resolves in seconds. When the
    // user's addons expose both kinds for a title, HTTP wins the default
    // selection. Within each tier (direct / magnet / other) the legacy
    // quality-size-name tiebreak is preserved so a user's muscle memory
    // for a specific 1080p pick inside a tier still ranks the same.
    std::stable_sort(out.begin(), out.end(),
        [](const StreamPickerChoice& a, const StreamPickerChoice& b) {
            // Tier 1: direct HTTP/Url first.
            if (a.isDirect != b.isDirect)                      return a.isDirect;
            // Tier 2 (within non-direct): magnet-with-seeders before
            // magnet-without-seeders-or-other-non-direct. Preserves the
            // legacy ordering *below* the new direct tier.

codex
The interaction with ranking looks clean: `isCamRip()` and `QualityScorer::sourceScore()` still receive `cand.title`, 
so the new blob only affects the identity gate. I’m doing one last pass for caller coverage and mechanical issues 
before I write the review.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'git diff --check -- 
src/core/stream/AutoSourcePicker.cpp src/core/stream/AutoSourcePicker.h src/ui/pages/StreamPage.cpp 
tests/core/stream/test_auto_source_picker.cpp' in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command 'rg -n "DOWNLOAD BUG 2026-06-03|stats 
badge|NyaaSi|MatchTextLetsBadge|MatchTextStillRejects|MatchTextBadgePicks|EmptyMatchTextFalls" src tests -S' in 
C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n 
\"SourceCandidate\\s|SourceCandidate\\{|\\.matchText|\\.title\\s*=\" src tests -S" in 
C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 2367ms:
tests\core\stream\test_auto_source_picker.cpp:136:// ── matchText identity blob (DOWNLOAD BUG 2026-06-03) 
───────────────────────
tests\core\stream\test_auto_source_picker.cpp:137:// NyaaSi / anime-shaped addons put a stats badge ("👤 2176 💾 1.34 
GB ⚙️ NyaaSi")
tests\core\stream\test_auto_source_picker.cpp:149:TEST(AutoSourcePicker, MatchTextLetsBadgeTitledOnePiecePassGate) {
tests\core\stream\test_auto_source_picker.cpp:150:    // title is the useless stats badge; the real filename lives in 
matchText.
tests\core\stream\test_auto_source_picker.cpp:152:        cm(QStringLiteral("👤 2176 💾 1.34 GB ⚙️ NyaaSi"),
tests\core\stream\test_auto_source_picker.cpp:153:           QStringLiteral("👤 2176 💾 1.34 GB ⚙️ 
NyaaSi\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
tests\core\stream\test_auto_source_picker.cpp:161:TEST(AutoSourcePicker, MatchTextStillRejectsWrongShowBadge) {
tests\core\stream\test_auto_source_picker.cpp:165:        cm(QStringLiteral("👤 5000 💾 50 GB ⚙️ NyaaSi"),
tests\core\stream\test_auto_source_picker.cpp:166:           QStringLiteral("👤 5000 💾 50 GB ⚙️ 
NyaaSi\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
tests\core\stream\test_auto_source_picker.cpp:172:TEST(AutoSourcePicker, 
MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
tests\core\stream\test_auto_source_picker.cpp:174:        cm(QStringLiteral("👤 5000 💾 50 GB ⚙️ NyaaSi"),
tests\core\stream\test_auto_source_picker.cpp:177:        cm(QStringLiteral("👤 800 💾 1.34 GB ⚙️ NyaaSi"),
tests\core\stream\test_auto_source_picker.cpp:186:TEST(AutoSourcePicker, EmptyMatchTextFallsBackToTitleGate) {
src\ui\pages\StreamPage.cpp:3362:    // DOWNLOAD BUG 2026-06-03 — build the show-identity match text from the RAW
src\ui\pages\StreamPage.cpp:3363:    // stream fields, not the cleaned displayTitle. NyaaSi / anime-shaped addons
src\ui\pages\StreamPage.cpp:3364:    // put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi") in displayTitle, which
src\ui\player\VideoPlayer.h:430:    // VIDEO_PLAYER_FIX Batch 7.1 — toggle the compact stats badge. State
src\ui\player\VideoPlayer.h:730:    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge state. Source metadata
src\ui\player\VideoPlayer.cpp:247:    // VIDEO_PLAYER_FIX Batch 7.1 — restore stats badge toggle. Applied
src\ui\player\VideoPlayer.cpp:2120:    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge (top-right overlay).
src\ui\player\VideoPlayer.cpp:3506:    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge: top-right, below toast
src\ui\player\KeyBindings.cpp:39:    // VIDEO_PLAYER_FIX Batch 7.1 — plain I for the compact stats badge
src\core\stream\StreamAggregator.h:104:    // DOWNLOAD BUG 2026-06-03 (review fix) — the SINGLE terminal emit path for
src\core\stream\StreamAggregator.cpp:563:    // DOWNLOAD BUG 2026-06-03 (review fix) — capture the generation this 
dispatch
src\core\stream\StreamAggregator.cpp:574:    // DOWNLOAD BUG 2026-06-03 (review fix) — count EVERY addon we are about 
to
src\core\stream\StreamAggregator.cpp:713:    // DOWNLOAD BUG 2026-06-03 (review fix) — deferred + generation-guarded 
so a
src\core\stream\AutoSourcePicker.h:16:    // DOWNLOAD BUG 2026-06-03 — text the show-identity gate matches against.
src\core\stream\AutoSourcePicker.h:17:    // `title` is the cleaned one-line display name, but some addons (NyaaSi /
src\core\stream\AutoSourcePicker.h:18:    // anime-shaped Torrentio) put a stats badge ("👤 2176 💾 1.34 GB ⚙️ NyaaSi")
src\core\stream\AutoSourcePicker.cpp:74:        // DOWNLOAD BUG 2026-06-03 — gate on matchText (full raw identity blob)
src\core\stream\AutoSourcePicker.cpp:75:        // when present; some addons put a stats badge in `title`, which lacks

 succeeded in 3133ms:
tests\core\test_tankorent_search_service.cpp:59:    r.title = "fake book";
tests\core\manga\MangaUpdatesDisambiguatorTest.cpp:16:    h.title = title;
tests\core\manga\MangaUpdatesDisambiguatorTest.cpp:25:    p.title = title;
tests\core\stream\test_auto_source_picker.cpp:7:static SourceCandidate c(const QString& title, int seeders, qint64 
sizeBytes, int qualitySort) {
tests\core\stream\test_auto_source_picker.cpp:8:    SourceCandidate s; s.title = title; s.seeders = seeders; 
s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
tests\core\stream\test_auto_source_picker.cpp:143:static SourceCandidate cm(const QString& title, const QString& 
matchText,
tests\core\stream\test_auto_source_picker.cpp:145:    SourceCandidate s; s.title = title; s.matchText = matchText;
tests\core\manga\AniListVolumeMapperTest.cpp:14:    c.title        = QString();
tests\core\manga\AniListVolumeMapperTest.cpp:23:    d.preview.title  = title;
src\ui\player\SubtitlePopover.cpp:283:    off.title = tr("Off");
src\ui\player\SubtitlePopover.cpp:307:            if (!title.isEmpty())          c.title = title;
src\ui\player\SubtitlePopover.cpp:308:            else if (!langHuman.isEmpty()) c.title = langHuman;
src\ui\player\SubtitlePopover.cpp:309:            else                           c.title = QStringLiteral("Track 
%1").arg(tid);
src\ui\player\SubtitlePopover.cpp:320:            c.title         = embeddedDisplayLabel(e);
src\ui\player\SubtitlePopover.cpp:331:        c.title    = addonDisplayLabel(a);
src\ui\player\SubtitlePopover.cpp:342:        c.title    = tr("Local file");
tests\core\manga\anilist\test_anilist_cache.cpp:21:    detail.preview.title        = QStringLiteral("Death Note");
tests\core\book\test_catalogue_rerank.cpp:8:    b.title  = title;
tests\core\book\test_catalogue_record.cpp:19:    r.title = QStringLiteral("Project Hail Mary");
src\core\book\AbbScraper.cpp:252:                r.title  = workingTitle.left(dashIdx).trimmed();
src\core\book\AbbScraper.cpp:254:                r.title = workingTitle;
src\core\book\AbbScraper.cpp:257:            r.title = workingTitle;
tests\core\book\test_book_catalogue_result.cpp:16:    r.title = QStringLiteral("Stormlight Archive");
tests\core\book\test_book_catalogue_result.cpp:30:    r.title = QStringLiteral("Project Hail Mary");
tests\core\book\test_books_catalogue_library_store.cpp:10:    r.title = title;
tests\core\book\test_books_catalogue_library_store.cpp:23:    r.title = title;
src\core\book\AnnaArchiveScraper.cpp:799:            r.title = o.value(QStringLiteral("title")).toString();
src\core\book\AnnaArchiveScraper.cpp:927:        r.title       = d.value(QStringLiteral("title")).toString();
src\ui\player\SidecarProcess.cpp:902:        row.title    = bestTrackTitle(t);
src\core\EpubParser.cpp:109:                        m_metadata.title = xml.readElementText();
src\core\EpubParser.cpp:162:        ch.title = QFileInfo(ch.href).completeBaseName(); // fallback title
src\core\EpubParser.cpp:202:                        ch.title = xml.readElementText();
src\core\EpubParser.cpp:238:                        ch.title = currentTitle;
src\core\indexers\ExtTorrentsIndexer.cpp:141:            lr.title = tm.captured(1).trimmed();
src\core\indexers\ExtTorrentsIndexer.cpp:145:                lr.title = lm.captured(1).trimmed();
src\core\indexers\ExtTorrentsIndexer.cpp:264:        r.title      = lr.title;
src\core\indexers\EztvIndexer.cpp:203:        r.title      = title;
src\core\indexers\NyaaIndexer.cpp:194:        r.title      = title;
src\core\indexers\PirateBayIndexer.cpp:74:        r.title      = name;
src\core\indexers\TorrentsCsvIndexer.cpp:76:        r.title     = name;
src\core\indexers\X1337xIndexer.cpp:220:            lr.title = dm.captured(2).trimmed();
src\core\indexers\X1337xIndexer.cpp:322:        r.title      = lr.title;
src\ui\pages\BooksPage.cpp:1221:    result.title = record.title;
src\ui\pages\BooksPage.cpp:1241:    r.title          = b.title;
src\ui\pages\BooksPage.cpp:1831:    r.title             = ctx.book.title;
src\ui\pages\BooksPage.cpp:1867:        info.title       = ctx.book.title;
src\core\indexers\YtsIndexer.cpp:104:            r.title      = rowTitle;
src\core\book\CatalogueRecord.cpp:41:    r.title           = o.value(QStringLiteral("title")).toString();
src\core\book\FictionDbClient.cpp:173:        r.title = ogTitle.left(byIdx).trimmed();
src\core\book\FictionDbClient.cpp:176:        r.title = ogTitle;
src\core\book\FictionDbClient.cpp:230:        b.title = titleFromSlug(slug);
src\core\book\FictionDbClient.cpp:265:        b.title = nm.hasMatch() ? nm.captured(1).trimmed() : 
titleFromSlug(um.captured(1));
src\core\book\LibGenScraper.cpp:319:            r.title = bestTitle;
src\core\book\LibGenScraper.cpp:322:            r.title = cellTexts[0].section(QChar('\n'), 0, 0).trimmed();
src\core\book\TankorentBookScraper.cpp:56:    r.title      = t.title;
src\ui\pages\books\BookSeriesDetailView.cpp:268:        if (!book.title.isEmpty()) p.title = book.title;
src\ui\pages\books\BookSeriesDetailView.cpp:433:        r.title          = b.title;
src\ui\pages\books\BookCatalogueDetailView.cpp:443:    if (!book.title.isEmpty())       m_currentBook.title       = 
book.title;
src\ui\pages\books\BookCatalogueDetailView.cpp:788:        r.title          = m_currentBook.title;
src\core\stream\AutoSourcePicker.h:10:struct SourceCandidate {
src\core\stream\AutoSourcePicker.cpp:78:        const QString& idText = cand.matchText.isEmpty() ? cand.title : 
cand.matchText;
src\core\stream\MetaAggregator.cpp:154:    v.title = obj.value(QStringLiteral("title")).toString().trimmed();
src\core\stream\MetaAggregator.cpp:156:        v.title = obj.value(QStringLiteral("name")).toString().trimmed();
src\core\stream\MetaAggregator.cpp:194:        item.title = 
videoObj.value(QStringLiteral("name")).toString().trimmed();
src\core\stream\MetaAggregator.cpp:196:            item.title = 
videoObj.value(QStringLiteral("title")).toString().trimmed();
src\core\manga\anilist\AniListParser.cpp:53:    p.title           = pickTitle(mediaObj.value("title").toObject());
src\core\manga\anilist\AniListCache.cpp:65:    p.title         = o.value("title").toString();
src\core\manga\anilist\AniListCache.cpp:137:        c.title       = co.value("title").toString();
src\core\manga\anilist\AniListCache.cpp:230:        info.title = o.value(QStringLiteral("title")).toString();
src\core\manga\ComicsLibraryRecord.cpp:35:    r.title               = j.value("title").toString();
src\core\manga\ComicsTankoyomiLibrary.cpp:146:                r.title    = meta->title;
src\core\manga\PremiumCatalog.cpp:102:        ref.title         = co.value(QStringLiteral("title")).toString();
src\core\manga\PremiumCatalog.cpp:158:    out.title           = obj.value(QStringLiteral("title")).toString();
src\core\manga\NyaaRuntimeSource.cpp:191:    NyaaSourceCandidate cur;
src\core\manga\NyaaRuntimeSource.cpp:204:                cur = NyaaSourceCandidate{};
src\core\manga\NyaaRuntimeSource.cpp:220:                cur.title = text;
src\core\manga\NyaaRuntimeSource.h:18:struct NyaaSourceCandidate {
src\core\manga\ReadComicsScraper.cpp:102:            r.title = m.captured(3).trimmed();
src\core\manga\ReadComicsScraper.cpp:104:            r.title = r.title.trimmed();
src\core\manga\MangaSeriesDetail.cpp:58:    d.preview.title        = j.value("title").toString();
src\core\manga\WeebCentralScraper.cpp:165:                r.title = inner;
src\core\manga\mangaupdates\VolumeMetadataResolver.cpp:91:    p.preview.title = p.englishTitle;
src\core\manga\mangaupdates\MangaUpdatesClient.cpp:192:        hit.title = 
rec.value(QStringLiteral("title")).toString();
src\core\manga\mangaupdates\MangaUpdatesClient.cpp:225:    info.title = rec.value(QStringLiteral("title")).toString();
src\ui\pages\ComicsPage.cpp:2448:    preview.title     = fallbackTitle;
src\ui\pages\ComicsPage.cpp:2497:    result.title        = record.title;
src\ui\pages\ComicsPage.cpp:2808:    rec.title    = displayTitle;
src\ui\pages\ComicsPage.cpp:3144:        if (rec.sourceId == source && rec.title == seriesTitle)
src\ui\pages\ComicsPage.cpp:3701:        synth.title            = seriesTitle;
src\ui\pages\ComicsPage.cpp:4446:            preview.title     = seriesTitle;
src\ui\pages\ComicsPage.cpp:4473:                result.title        = rec.title;
src\ui\pages\comics\ComicsSeriesView.cpp:994:    preview.title     = title;
src\ui\pages\comics\ComicsSeriesView.cpp:1260:        data.title        = synthesizedTitle;
src\ui\pages\comics\ComicsSeriesView.cpp:1441:        data.title        = !vol.titleEnglish.isEmpty() ? 
vol.titleEnglish : vol.titleJapanese;
src\ui\pages\comics\ComicsSeriesView.cpp:1566:        xData.title        = QStringLiteral("Volume X");
src\ui\pages\comics\ComicsSourceCard.cpp:279:    m_row.title = title;
src\ui\pages\comics\ComicsSourcesPanel.cpp:225:            row.title = volEntry.cbzFileName.isEmpty()
src\ui\pages\comics\ComicsSourcesPanel.cpp:281:    wcRow.title = QStringLiteral("WeebCentral");
src\ui\pages\comics\ComicsSourcesPanel.cpp:365:        row.title = cand.title;
src\ui\pages\comics\SidecarMeta.cpp:26:    m.title         = o.value("title").toString();
src\ui\pages\StreamPage.cpp:932:                            row.title = episode.title;
src\ui\pages\StreamPage.cpp:3046:        ep.title = episode.title;
src\ui\pages\StreamPage.cpp:3386:        tankostream::stream::SourceCandidate sc;
src\ui\pages\StreamPage.cpp:3387:        sc.title       = c.displayTitle;
src\ui\pages\StreamPage.cpp:3388:        sc.matchText    = identityBlob(c);
src\ui\pages\StreamPage.cpp:3512:        ep.title = episode.title;
src\ui\pages\StreamPage.cpp:3650:        row.title = ep.title;
src\ui\pages\TankoLibraryPage.cpp:2088:    rec.title = title;
src\ui\pages\stream\CatalogBrowseScreen.cpp:326:            item.title = c.name.isEmpty() ? c.id : c.name;
src\ui\pages\TankorentPage.cpp:1749:        seg.title  = r.title;
src\ui\pages\stream\TheatreDownloadPanel.cpp:975:        d.title   = m_selectedPack.raw.title;
src\ui\pages\stream\TheatreDownloadPanel.cpp:1085:        d.title = m_selectedPack.raw.title;
src\ui\pages\stream\TheatreDownloadPanel.cpp:1130:            d.title   = ep.title;
src\ui\pages\stream\TheatreDownloadPanel.cpp:1144:                        d.title = fileDisplayPath(f).section('/', 
-1).section('\\', -1);
src\ui\pages\stream\TheatreDownloadPanel.cpp:1242:                    est.title = fileDisplayPath(f).section('/', 
-1).section('\\', -1);  // basename

 succeeded in 3326ms:
warning: in the working copy of 'src/core/stream/AutoSourcePicker.cpp', LF will be replaced by CRLF the next time Git 
touches it
warning: in the working copy of 'src/core/stream/AutoSourcePicker.h', LF will be replaced by CRLF the next time Git 
touches it
warning: in the working copy of 'tests/core/stream/test_auto_source_picker.cpp', LF will be replaced by CRLF the next 
time Git touches it

codex
One mechanical issue did show up: the new comments and test literals include emoji in source files, which conflicts 
with the repo’s “no emojis in code/docs” governance. I’m separating that from the runtime behavior assessment so the 
approval line is clear.
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg -n \"No emojis|emoji|emojis|Scoped 
CSS|gray/black/white\" AGENTS.md agents\\GOVERNANCE.md -S" in C:\Users\Suprabha\Desktop\Tankoban 2
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg --files | rg 
\"(test_auto_source_picker|AutoSourcePicker|build_check|ctest|CMakeLists\\.txt)"'$"' in 
C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1991ms:
agents\GOVERNANCE.md:216:- All other brotherhood rules (1, 11, 14, 15, 17, CSS scoping, no color/emoji, etc.) apply 
identically to Trigger D work.
agents\GOVERNANCE.md:233:- All other brotherhood rules (1, 11, 14, 15, 17, 19, CSS scoping, no color/emoji, etc.) 
apply identically to Trigger E work.
AGENTS.md:13:You are a normal code assistant working in the Tankoban 2 repository. Read files, answer questions, draft 
code, run grep, help with whatever the user asks. Respect the codebase conventions (no emojis, gray/black/white UI, 
scoped CSS, etc. — see `agents/GOVERNANCE.md` for the full list), but you are NOT restricted to prototype/audit mode.
AGENTS.md:154:- **feedback_no_color_no_emoji:** strictly gray/black/white UI, no emojis, SVG icons only.
AGENTS.md:256:- **No emojis** in code, docs, or commit messages. Strictly gray/black/white UI (see 
`feedback_no_color_no_emoji` if user references memory).
AGENTS.md:257:- **Scoped CSS** only (`#ObjectName` selectors, never bare `background: transparent`).

2026-06-03T09:24:21.410923Z ERROR codex_core::tools::router: error=Exit code: 1
Wall time: 2.3 seconds
Output:

 exited 1 in 2285ms:
exec
"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -Command "rg --files tests src | rg 
\"test_auto_source_picker|CMakeLists\\.txt|build_check\\.bat\"" in C:\Users\Suprabha\Desktop\Tankoban 2
 succeeded in 1402ms:
tests\core\stream\test_auto_source_picker.cpp

codex
**Findings**
- [test_auto_source_picker.cpp](</abs/path/C:/Users/Suprabha/Desktop/Tankoban 
2/tests/core/stream/test_auto_source_picker.cpp:137>), 
[AutoSourcePicker.h](</abs/path/C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/AutoSourcePicker.h:18>), 
[StreamPage.cpp](</abs/path/C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:3364>): the patch adds 
emoji literals/comments to code and tests. That violates the repo governance injected in `AGENTS.md` (“No emojis in 
code, docs, or commit messages”). Use ASCII badge text in comments/tests; the test only needs a title that lacks 
`one`/`piece`.

Behavior review:
- The One Piece case is unblocked: `pick()` gates on `matchText` when set, and `titleMatchesShow("[SubsPlease] One 
Piece - 1164...", "One Piece")` passes because normalization turns separators/newlines into spaces and both whole-word 
tokens are present.
- Wrong-show protection is preserved for the intended Community-style case: if the blob is only `Community...`, it 
still fails. The broader blob can false-match if an included field literally contains all significant show tokens as 
unrelated prose, especially `stream.description`, but no addon name is included and whole-token matching keeps 
incidental partial matches out.
- Empty `matchText` falls back to `title`; `rg` found no non-test caller setting `matchText` except 
`finishAutoDownloadPick()`.
- The blob fields exist on `StreamPickerChoice` / `Stream` / `StreamBehaviorHints`; 
`QVariantMap::value(...).toString()` is null-safe. Joining with `'\n'` is safe for the matcher.
- `isCamRip()` and `sourceScore()` still read `cand.title`, not `matchText`.
- The new tests are not tautologies; the badge-title pass and “pick One Piece over better-seeded wrong show” cases 
would fail under the old title-only gate.

REQUEST-CHANGES: Runtime behavior looks correct, but the patch must remove emoji literals/comments from source and 
test files to comply with repository governance.
tokens used
56,942
**Findings**
- [test_auto_source_picker.cpp](</abs/path/C:/Users/Suprabha/Desktop/Tankoban 2/tests/core/stream/test_auto_source_picker.cpp:137>), [AutoSourcePicker.h](</abs/path/C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/AutoSourcePicker.h:18>), [StreamPage.cpp](</abs/path/C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:3364>): the patch adds emoji literals/comments to code and tests. That violates the repo governance injected in `AGENTS.md` (“No emojis in code, docs, or commit messages”). Use ASCII badge text in comments/tests; the test only needs a title that lacks `one`/`piece`.

Behavior review:
- The One Piece case is unblocked: `pick()` gates on `matchText` when set, and `titleMatchesShow("[SubsPlease] One Piece - 1164...", "One Piece")` passes because normalization turns separators/newlines into spaces and both whole-word tokens are present.
- Wrong-show protection is preserved for the intended Community-style case: if the blob is only `Community...`, it still fails. The broader blob can false-match if an included field literally contains all significant show tokens as unrelated prose, especially `stream.description`, but no addon name is included and whole-token matching keeps incidental partial matches out.
- Empty `matchText` falls back to `title`; `rg` found no non-test caller setting `matchText` except `finishAutoDownloadPick()`.
- The blob fields exist on `StreamPickerChoice` / `Stream` / `StreamBehaviorHints`; `QVariantMap::value(...).toString()` is null-safe. Joining with `'\n'` is safe for the matcher.
- `isCamRip()` and `sourceScore()` still read `cand.title`, not `matchText`.
- The new tests are not tautologies; the badge-title pass and “pick One Piece over better-seeded wrong show” cases would fail under the old title-only gate.

REQUEST-CHANGES: Runtime behavior looks correct, but the patch must remove emoji literals/comments from source and test files to comply with repository governance.
