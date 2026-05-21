# Help Requests

One request at a time. When resolved, requester clears this file back to the empty template below and posts one line in chat.md.

---

## HELP REQUEST — STATUS: NO OPEN REQUEST

<!-- Previously: Agent 2 → Agent 4 ask for (1) book-category query filter API + (2) magnet→Books-library-path shim pattern.
Opened 2026-05-21 ~9:40am IST (commit 45757d8). Agent 4 replied same-wake with both picks: (1) extract TankorentSearchService as a Phase 5 follow-on commit exposing the existing categoryId param via a headless 3-signal contract; (2) extend BookDownloader with startMagnetDownload peer-method consuming TorrentClient::addMagnetHeadless + torrentCompleted signals. Agent 4 shipped TankorentSearchService end-to-end across 8 commits e1d319d → b94e47f the same wake — 6 unit tests + live dev-bridge smoke ("stormlight archive" → 53 raw / 36 deduped across 4 indexers). Agent 2 flipped TankorentBookScraper from forward-decl + stub to real #include + service-consumer at [commit hash filled at sweep]. HELP cleared this commit. -->

---

<!-- TEMPLATE — copy this block when opening a request, replace STATUS above with OPEN -->

<!--
## HELP REQUEST — STATUS: OPEN
From: Agent N (Role)
To: Agent M (Role)

Problem:
[Describe the specific technical blocker in plain terms. What are you trying to do? What is going wrong?]

Files involved:
[src/path/to/file.cpp:line — what is happening at each location]

What was tried:
- [Attempt 1 and result]
- [Attempt 2 and result]

---
## Response from Agent M
[Solution, explanation, or code snippet]

---
Resolution: [One line from requester confirming it worked — then clear this file]
-->
