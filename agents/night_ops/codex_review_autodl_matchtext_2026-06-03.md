Cross-model review for Tankoban 2 (requested by Agent 4, author = Agent 4/Opus). You are a DIFFERENT model than the author. Read-only review — do NOT edit any file; just report.

CONTEXT: Live-smoke follow-up to this morning's A001 stream-load-correlation fix. Hemanth clicked Download on One Piece episode 1164 and got "No 1080p source found". The [auto-dl] diagnostic logging (left in for exactly this) proved:
  - The request correlated to the RIGHT show: `req.id= kitsu:12:1164 showTitle= One Piece`, and 10 real One Piece torrents arrived (NyaaSi/ThePirateBay, up to 2176 seeders). So A001 (correlation) is WORKING — the right sources reached the handler.
  - But EVERY candidate logged `gate= false` and the pick returned NONE. Root cause: the auto-download show-identity gate (AutoSourcePicker show-token match) was fed `StreamPickerChoice.displayTitle`, which for these NyaaSi/anime-shaped addons is a STATS BADGE ("👤 2176 💾 1.34 GB ⚙️ NyaaSi"), not the release filename. The badge contains neither "one" nor "piece", so titleMatchesShow rejected all 10 → "No 1080p source found".

This is a separate, older bug behind the (now-fixed) correlation race. The gate is correct in intent (it exists to stop "One Piece → Community" wrong-show auto-downloads); it was just matching the wrong field.

THE FIX UNDER REVIEW (4 files):
  1. AutoSourcePicker.h — add `QString matchText;` to SourceCandidate. Doc: the text the identity gate matches against; empty == fall back to `title`.
  2. AutoSourcePicker.cpp — in the gated pick(), the show-identity check now reads `matchText` when non-empty, else `title`:
        const QString& idText = cand.matchText.isEmpty() ? cand.title : cand.matchText;
        if (!showTitle.isEmpty() && !titleMatchesShow(idText, showTitle)) continue;
     `cand.title` is STILL used for isCamRip() and the sourceScore() tiebreak — unchanged.
  3. StreamPage.cpp finishAutoDownloadPick() — populate sc.matchText with a raw identity blob joined from displayTitle + stream.name + stream.description + stream.source.fileNameHint + choice.fileNameHint + behaviorHints.filename + behaviorHints.other["parsedFilename"]. sc.title stays displayTitle. The [auto-dl] diagnostic is enhanced to log gateBlob vs gateOld + the blob, so the re-smoke is conclusive.
  4. test_auto_source_picker.cpp — 4 new tests: badge-title + matchText-with-show passes; badge + wrong-show matchText still rejected; better-seeded wrong-show badge skipped for One Piece; empty matchText falls back to title gate.

VERIFY — do all of:
1. Does the fix actually unblock the One Piece case? With a badge `title` and a `matchText` blob containing "[SubsPlease] One Piece - 1164 (1080p).mkv", does titleMatchesShow(blob, "One Piece") pass and the candidate get picked?
2. Is the wrong-show protection PRESERVED (not weakened)? A blob is a multi-field join — could a wrong-show release now FALSE-MATCH because some unrelated field happens to contain the show's tokens (e.g. a "related: One Piece" mention, addon name, or a pack that lists many shows)? Assess the realistic risk given titleMatchesShow requires EVERY significant show token as a whole word.
3. Fallback correctness: when matchText is empty, is behavior byte-for-byte identical to before (gate on title)? Confirm no existing caller other than finishAutoDownloadPick sets matchText (it defaults empty).
4. The identityBlob lambda in StreamPage.cpp — any field that doesn't exist / wrong accessor / null-deref risk on stream.behaviorHints.other? Is joining with '\n' safe for titleMatchesShow's normalization (it replaces non-alphanumerics with spaces, so newlines are fine — confirm)?
5. Does feeding the blob to the gate interact badly with isCamRip or the sourceScore tiebreak? (It should NOT — those still read cand.title, not matchText. Confirm.)
6. Any scope creep or NEW bug introduced by the struct field / lambda / diagnostic change.
7. Sanity-check the 4 new tests actually encode the contract (no tautologies; the badge cases would FAIL against the old title-only gate).

END with exactly one line: APPROVE or REQUEST-CHANGES + one-sentence reason. Default REQUEST-CHANGES if the fix doesn't unblock One Piece, weakens wrong-show protection, or you are unsure.

================ DIFF UNDER REVIEW ================
DIFF_PLACEHOLDER
