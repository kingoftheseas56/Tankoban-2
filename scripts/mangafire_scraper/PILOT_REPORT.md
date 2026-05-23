# MangaFire Pilot Report - 2026-05-23

Run command:

```powershell
python scripts\mangafire_scraper\mangafire_ingest.py https://mangafire.to/manga/one-piecee.dkw https://mangafire.to/manga/berserkk.m2vv https://mangafire.to/manga/20th-century-boyss.kwj4 https://mangafire.to/manga/a-silent-voicee.7p56 https://mangafire.to/manga/yotsubaa.n7j https://mangafire.to/manga/dandadann.3r5x9 https://mangafire.to/manga/vinland-sagaa.zpw https://mangafire.to/manga/mushishii.0x03 https://mangafire.to/manga/sousou-no-frierenn.2v70 https://mangafire.to/manga/death-notee.p46 --metrics-out scripts\mangafire_scraper\pilot_metrics.json
```

## Per-series results

### One Piece

- Slug: `one-piece`
- Output: `data/mangafire_catalog/one-piece.json`
- Volume count: MangaFire extracted 117; AniList/Jikan currently report null for ongoing title.
- Cover sample: `https://static.mfcdn.nl/f935/i/9/98/9858f617762a1011b82cbd3fb6ef5ce5.jpg`, `https://static.mfcdn.nl/bff6/i/e/ea/ea86c0c9ef7835004fbc3510989e6b19.jpg`, `https://static.mfcdn.nl/725e/i/e/ea/ead6a329b890ff54ef6478dcef317efa.jpg`
- Chapter range sample: Vol 1 `1`-`8.5`; Vol 2 `9`-`17.5`; Vol 3 `18`-`26.5`
- Empty fields: `studio`, `publishedYearEnd`; 3 newest volume covers are placeholders upstream.

### Berserk

- Slug: `berserk`
- Output: `data/mangafire_catalog/berserk.json`
- Volume count: MangaFire extracted 44; AniList/Jikan currently report null for ongoing title.
- Cover sample: `https://static.mfcdn.nl/b17b/i/e/e7/e77086f55a01dbfdb1549356b8f21b14.jpg`, `https://static.mfcdn.nl/de4b/i/7/70/708ebea710c5f6a96eb53e4a2508634e.jpg`, `https://static.mfcdn.nl/4e0c/i/0/0c/0cadaf8220af5b83936387511e1e30c2.jpg`
- Chapter range sample: Vol 1 `0.01`-`0.03`; Vol 2 `0.04`-`0.05`; Vol 3 `0.06`-`0.09`
- Empty fields: `publishedYearEnd`; 2 newest volume covers are placeholders upstream.

### 20th Century Boys

- Slug: `20th-century-boys`
- Output: `data/mangafire_catalog/20th-century-boys.json`
- Volume count: MangaFire extracted 22; AniList/Jikan ground truth 22.
- Cover sample: `https://static.mfcdn.nl/d974/i/3/32/32e416fd3b8cdca6db624fe90d44b5a6.jpg`, `https://static.mfcdn.nl/75bd/i/5/59/5952c45ec573e16fa5524186d520da49.jpg`, `https://static.mfcdn.nl/4653/i/7/79/79f1d08ba2f57571f98a83d1ba9345d5.jpg`
- Chapter range sample: Vol 1 `1`-`10`; Vol 2 `11`-`21`; Vol 3 `22`-`32`
- Empty fields: `studio`.

### A Silent Voice

- Slug: `a-silent-voice`
- Output: `data/mangafire_catalog/a-silent-voice.json`
- Volume count: MangaFire extracted 7; AniList/Jikan ground truth 7.
- Cover sample: `https://static.mfcdn.nl/9d8a/i/f/fd/fd6f0f9cbdf43445c2d79f2933205fe2.jpg`, `https://static.mfcdn.nl/fb01/i/1/18/18b7868de2bf296ccc217ed960016e7d.jpg`, `https://static.mfcdn.nl/4957/i/3/3e/3e1e8702eec843ab659d6b5992d0657d.jpg`
- Chapter range sample: Vol 1 `1`-`5`; Vol 2 `6`-`14`; Vol 3 `15`-`23`
- Empty fields: `studio`.

### Yotsuba&!

- Slug: `yotsuba`
- Output: `data/mangafire_catalog/yotsuba.json`
- Volume count: MangaFire extracted 17; AniList/Jikan currently report null for ongoing title.
- Cover sample: `https://static.mfcdn.nl/c3c8/i/e/23/4c075afbb67d848018b23dc577516e17.jpg`, `https://static.mfcdn.nl/1ff2/i/0/44/95318df9315794fdb6081462f428dbfd.jpg`, `https://static.mfcdn.nl/4bad/i/d/8d/d6119d3eab0b0793de46edbadf4c884f.jpg`
- Chapter range sample: Vol 1 `1`-`7`; Vol 2 `8`-`14`; Vol 3 `15`-`21`
- Empty fields: `studio`, `publishedYearEnd`; 2 newest volume covers are placeholders upstream.

### Dandadan

- Slug: `dandadan`
- Output: `data/mangafire_catalog/dandadan.json`
- Volume count: MangaFire extracted 24; AniList/Jikan currently report null for ongoing title.
- Cover sample: `https://static.mfcdn.nl/2df6/i/a/a2/a2d2416c0ea9c6c2286dcbe9916ed91c.jpg`, `https://static.mfcdn.nl/05f3/i/8/82/d091d6cd387effbf689ec2016ab8e3cb.jpg`, `https://static.mfcdn.nl/4a55/i/2/20/3f8afc1dc32d49f7876dc1aceef5ab26.jpg`
- Chapter range sample: Vol 1 `1`-`5.5`; Vol 2 `6`-`14`; Vol 3 `15`-`23`
- Empty fields: `studio`, `publishedYearEnd`; 4 newest volume covers are placeholders upstream.

### Vinland Saga

- Slug: `vinland-saga`
- Output: `data/mangafire_catalog/vinland-saga.json`
- Volume count: MangaFire extracted 29; AniList ground truth 29; Jikan/MAL ground truth 29.
- Cover sample: `https://static.mfcdn.nl/2306/i/9/79/baa57ba8504d86d20f55f776ce2f8e43.jpg`, `https://static.mfcdn.nl/bb4f/i/1/bd/69be7d0e08b6bf05abfb81e7f9f58834.jpg`, `https://static.mfcdn.nl/920b/i/6/6b/0c981e70a9cd42daf87b0aa815d629d4.jpg`
- Chapter range sample: Vol 1 `1`-`16.6`; Vol 2 `6`-`28.4`; Vol 3 `17`-`42.6`
- Empty fields: `studio`, `publishedYearEnd`; one newest volume cover is a placeholder upstream. The chapter endpoint has fractional/special chapters assigned into early volumes, so the range sample overlaps.

### Mushishi

- Slug: `mushishi`
- Output: `data/mangafire_catalog/mushishi.json`
- Volume count: MangaFire extracted 10; AniList/Jikan ground truth 10.
- Cover sample: `https://static.mfcdn.nl/0e2e/i/8/78/75c52db78d5919d0cc06c30212c1cb6b.jpg`, `https://static.mfcdn.nl/04b5/i/4/00/de269f4d13e2b623be96e4b302f92410.jpg`, `https://static.mfcdn.nl/f88e/i/b/0f/1c85aba1166d915d95f62bbd89ee0995.jpg`
- Chapter range sample: Vol 1 `1`-`5.3`; Vol 2 `6`-`10.2`; Vol 3 `11`-`15.2`
- Empty fields: `studio`.

### Frieren: Beyond Journey's End

- Slug: `frieren-beyond-journey-s-end`
- Output: `data/mangafire_catalog/frieren-beyond-journey-s-end.json`
- Volume count: MangaFire extracted 15; AniList/Jikan currently report null for ongoing title.
- Cover sample: `https://static.mfcdn.nl/c9fa/i/0/0a/0aa6477b540a437409fc05d2355433b4.jpg`, `https://static.mfcdn.nl/d6c7/i/2/2e/2e1ca444d0ade5ddce0029621f5ac214.jpg`, `https://static.mfcdn.nl/fa8a/i/f/f1/f1348f236e681809d0efaad8761fc7d6.jpg`
- Chapter range sample: Vol 1 `1`-`7`; Vol 2 `8`-`17`; Vol 3 `18`-`27`
- Empty fields: `studio`, `publishedYearEnd`.

### Death Note

- Slug: `death-note`
- Output: `data/mangafire_catalog/death-note.json`
- Volume count: MangaFire extracted 13; AniList/Jikan main-series ground truth 12.
- Cover sample: `https://static.mfcdn.nl/9786/i/0/27/71a861768a3e3c9861d1b78c3aaccfec.jpg`, `https://static.mfcdn.nl/414b/i/8/57/558145a6e74db74fbc2c4b4de33903fb.jpg`, `https://static.mfcdn.nl/59bb/i/2/3c/b5aa2649f47d610a99f63dc693b0e59a.jpg`
- Chapter range sample: Vol 1 `1`-`7`; Vol 2 `8`-`16`; Vol 3 `17`-`25`
- Empty fields: `studio`. MangaFire includes a 13th volume bucket; likely a guide/extra relative to the 12-volume main-series count.

## Aggregate

- Pilot outputs: 10/10 series succeeded.
- Total nonzero volumes extracted: 298.
- Volume cover coverage: 286/298 = 96.0%.
- Chapter range coverage: 298/298 = 100.0%.
- Series metadata coverage across tracked fields: 125/140 = 89.3%.
- Combined raw field coverage including intentionally empty per-volume `title` and `synopsis`: 1007/1630 = 61.8%.
- Average per-series ingest time: 3.11 seconds measured inside each worker. Wall-clock for the explicit 10-URL pilot was 10.0 seconds with 4 workers.

## Failure modes and caveats

- MangaFire exposes a `Vol 0` bucket on every pilot series. It is an extras/unassigned bucket, not a tankobon volume, and the script omits it.
- Newest covers can be upstream placeholders. The script falls back from English covers to Japanese covers, then emits an empty cover URL if both are placeholders.
- Per-volume title and synopsis are not present in the volume-list endpoint. Those fields remain empty by design.
- Ongoing series often have null AniList/Jikan volume counts, so "ground truth" is unavailable from those APIs without a different reference.
- Some chapter range mappings include fractional/special chapters in surprising upstream volume buckets, visible on Vinland Saga.
- No Cloudflare 429 responses occurred during the pilot. Plain Python `requests` worked for page, sitemap, and AJAX catalog endpoints.

## Full-corpus projection

- Sitemap corpus: 53,313 manga URLs across 54 `sitemap-list-N.xml` files.
- Projected runtime at the pilot average of 3.11 seconds/series with 4 workers: about 11.5 hours of worker time, roughly 3 hours wall-clock before rate-limit margin.
- Recommended production pace: start at 30-60 series/minute, hold the script's 200ms inter-request backoff, and drop to 1 request/second if any 429 or challenge page appears.
- Main blockers for a 1000-series run: placeholder covers on newest volumes, upstream `Vol 0` extras, title resolution collisions when running from title strings instead of sitemap URLs, and possible Cloudflare behavior changes over a long run.

Verdict for full-corpus run: **YELLOW-GREEN**. The source is strong enough for a 1000-series staged run, but do it in sitemap chunks with retry/429 monitoring and post-run validation for placeholder covers and volume-count anomalies.
