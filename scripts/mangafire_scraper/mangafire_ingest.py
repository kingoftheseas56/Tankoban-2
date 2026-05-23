#!/usr/bin/env python3
"""Ingest MangaFire series metadata into Tankoban's catalog JSON shape.

The scraper prefers MangaFire's JSON-wrapped HTML endpoints:
    /ajax/manga/<hash>/volume/<lang>
    /ajax/manga/<hash>/chapter/<lang>

It intentionally does not fetch page images. For catalog work we only need
series metadata, volume covers, and chapter ranges.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import urljoin, urlparse

import requests
from bs4 import BeautifulSoup


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OUTPUT_DIR = REPO_ROOT / "data" / "mangafire_catalog"
BASE_URL = "https://mangafire.to"
SITEMAP_URL = f"{BASE_URL}/sitemap.xml"
NO_IMAGE_MARKERS = (
    "/assets/t2/s1/images/no-image.jpg",
    "/assets/sites/mangafire/logo",
)
SCRIPT_VERSION = "mangafire_ingest.py v1"


class RateLimiter:
    def __init__(self, delay_seconds: float = 0.2) -> None:
        self.delay_seconds = delay_seconds
        self._lock = threading.Lock()
        self._next_at = 0.0

    def wait(self) -> None:
        with self._lock:
            now = time.monotonic()
            if self._next_at > now:
                time.sleep(self._next_at - now)
            self._next_at = time.monotonic() + self.delay_seconds


@dataclass
class FetchResult:
    text: str
    elapsed_seconds: float
    final_url: str


class MangaFireClient:
    def __init__(self, delay_seconds: float = 0.2) -> None:
        self.session = requests.Session()
        self.session.headers.update(
            {
                "User-Agent": (
                    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/124.0 Safari/537.36"
                ),
                "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,application/json;q=0.8,*/*;q=0.7",
                "Referer": f"{BASE_URL}/",
            }
        )
        self.rate_limiter = RateLimiter(delay_seconds)

    def get(self, url: str, *, accept_json: bool = False, timeout: float = 30.0) -> FetchResult:
        headers = {}
        if accept_json:
            headers.update(
                {
                    "Accept": "application/json, text/javascript, */*; q=0.01",
                    "X-Requested-With": "XMLHttpRequest",
                }
            )

        for attempt in range(3):
            self.rate_limiter.wait()
            start = time.perf_counter()
            response = self.session.get(url, headers=headers, timeout=timeout)
            elapsed = time.perf_counter() - start

            if response.status_code == 429:
                retry_after = response.headers.get("Retry-After")
                delay = int(retry_after) if retry_after and retry_after.isdigit() else 5
                time.sleep(delay)
                continue

            if response.status_code >= 500 and attempt < 2:
                time.sleep(1.0 + attempt)
                continue

            response.raise_for_status()
            response.encoding = response.encoding or "utf-8"
            return FetchResult(response.text, elapsed, response.url)

        raise RuntimeError(f"GET failed after retries: {url}")


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def slugify(title: str) -> str:
    s = title.lower().strip()
    s = re.sub(r"[^a-z0-9]+", "-", s)
    return re.sub(r"-+", "-", s).strip("-")


def normalize_status(status: str) -> str:
    value = status.strip().upper().replace(" ", "_")
    mapping = {
        "ONGOING": "RELEASING",
        "RELEASING": "RELEASING",
        "COMPLETED": "COMPLETED",
        "FINISHED": "COMPLETED",
        "HIATUS": "HIATUS",
        "CANCELLED": "CANCELLED",
        "CANCELED": "CANCELLED",
    }
    return mapping.get(value, value)


def normalize_url(url: str) -> str:
    if not url:
        return ""
    return urljoin(BASE_URL, url)


def is_placeholder_image(url: str) -> bool:
    return not url or any(marker in url for marker in NO_IMAGE_MARKERS)


def manga_fire_id_from_url(url: str) -> str:
    path = urlparse(url).path.rstrip("/")
    return path.rsplit("/", 1)[-1]


def manga_hash_from_id(mangafire_id: str) -> str:
    return mangafire_id.rsplit(".", 1)[-1]


def parse_int_year(text: str) -> int | None:
    m = re.search(r"\b(19\d{2}|20\d{2})\b", text or "")
    return int(m.group(1)) if m else None


def parse_float(text: str) -> float | None:
    m = re.search(r"\b\d+(?:\.\d+)?\b", text or "")
    return float(m.group(0)) if m else None


def text_list(nodes: list[Any]) -> list[str]:
    out: list[str] = []
    for node in nodes:
        value = node.get_text(" ", strip=True) if hasattr(node, "get_text") else str(node)
        value = re.sub(r"\s+", " ", value).strip(" ,")
        if value:
            out.append(value)
    return out


def parse_meta_field(meta: BeautifulSoup, label: str) -> list[str]:
    field = meta.select_one(f"span:-soup-contains('{label}')")
    if not field:
        return []
    sibling = field.find_next_sibling("span")
    if not sibling:
        return []
    links = text_list(list(sibling.select("a")))
    if links:
        return links
    return [sibling.get_text(" ", strip=True).strip()]


def parse_series_page(html: str, url: str) -> dict[str, Any]:
    soup = BeautifulSoup(html, "html.parser")
    detail = soup.select_one(".main-inner:not(.manga-bottom)") or soup.select_one(".manga-detail") or soup

    title = (detail.select_one("h1") or soup.select_one("h1"))
    series_title = title.get_text(" ", strip=True) if title else ""

    alt_raw = ""
    h6 = detail.select_one("h6")
    if h6:
        alt_raw = h6.get_text(" ", strip=True)
    alternatives = [v.strip() for v in re.split(r"\s*;\s*", alt_raw) if v.strip()]
    alternatives = [v for v in alternatives if v != series_title]

    status_node = detail.select_one(".info > p")
    status = normalize_status(status_node.get_text(" ", strip=True) if status_node else "")

    synopsis_node = soup.select_one("#synopsis .modal-content") or detail.select_one(".description")
    synopsis = synopsis_node.get_text(" ", strip=True) if synopsis_node else ""

    meta = detail.select_one(".meta") or soup
    author_values = parse_meta_field(meta, "Author")
    studios = [value for value in author_values if re.search(r"\bstudio\b", value, re.IGNORECASE)]
    authors = [value for value in author_values if value not in studios]
    genres = parse_meta_field(meta, "Genres")
    magazines = parse_meta_field(meta, "Mangazines")
    published_text = " ".join(parse_meta_field(meta, "Published"))
    years = [int(y) for y in re.findall(r"\b(19\d{2}|20\d{2})\b", published_text)]

    min_info = detail.select_one(".min-info")
    mal_score = None
    if min_info:
        mal_node = min_info.select_one("span b:-soup-contains('MAL')")
        mal_score = parse_float(mal_node.get_text(" ", strip=True) if mal_node else "")

    return {
        "seriesTitle": series_title,
        "seriesTitleAlt": alternatives,
        "mangafireUrl": url,
        "mangafireId": manga_fire_id_from_url(url),
        "author": ", ".join(authors),
        "studio": ", ".join(studios),
        "genres": genres,
        "status": status,
        "publishedYearStart": years[0] if years else parse_int_year(published_text),
        "publishedYearEnd": years[-1] if len(years) > 1 else None,
        "mangazine": ", ".join(magazines),
        "malScoreRaw": mal_score,
        "synopsis": synopsis,
    }


def parse_json_fragment(text: str) -> BeautifulSoup:
    payload = json.loads(text)
    if payload.get("status") != 200:
        raise RuntimeError(f"MangaFire ajax status was {payload.get('status')}")
    return BeautifulSoup(payload.get("result") or "", "html.parser")


def fetch_volume_fragment(client: MangaFireClient, manga_hash: str, lang: str, referer: str) -> tuple[BeautifulSoup, float]:
    client.session.headers["Referer"] = referer
    result = client.get(f"{BASE_URL}/ajax/manga/{manga_hash}/volume/{lang}", accept_json=True)
    return parse_json_fragment(result.text), result.elapsed_seconds


def fetch_chapter_fragment(client: MangaFireClient, manga_hash: str, lang: str, referer: str) -> tuple[BeautifulSoup, float]:
    client.session.headers["Referer"] = referer
    result = client.get(f"{BASE_URL}/ajax/manga/{manga_hash}/chapter/{lang}", accept_json=True)
    return parse_json_fragment(result.text), result.elapsed_seconds


def parse_volume_list(fragment: BeautifulSoup) -> dict[int, dict[str, str]]:
    volumes: dict[int, dict[str, str]] = {}
    for item in fragment.select(".vol-list > .item, .item"):
        number_raw = item.get("data-number") or ""
        if not number_raw.isdigit():
            continue
        number = int(number_raw)
        if number <= 0:
            continue
        link = item.select_one("a")
        img = item.select_one("img")
        label = item.select_one("span")
        volumes[number] = {
            "number": number_raw,
            "url": normalize_url(link.get("href", "")) if link else "",
            "coverUrl": normalize_url(img.get("src", "")) if img else "",
            "label": label.get_text(" ", strip=True) if label else "",
        }
    return volumes


def chapter_sort_key(raw: str) -> tuple[float, str]:
    parts = re.findall(r"-?\d+(?:\.\d+)?", raw)
    if not parts:
        return (float("inf"), raw)
    if len(parts) >= 2 and raw.strip().startswith("0-"):
        value = float(f"0.{int(parts[1]):04d}")
    else:
        value = float(parts[-1])
    return (value, raw)


def extract_chapter_id(link: str, title: str, data_number: str) -> str:
    m = re.search(r"/chapter-([^/#?]+)", link or "")
    if m:
        return m.group(1)
    m = re.search(r"\bChap(?:ter)?\s+([^\s]+)", title or "", re.IGNORECASE)
    if m:
        return m.group(1)
    return data_number


def parse_chapter_ranges(fragment: BeautifulSoup) -> dict[int, tuple[str, str]]:
    grouped: dict[int, list[str]] = {}
    for item in fragment.select("li.item, .item"):
        link = item.select_one("a")
        if not link:
            continue
        title_attr = link.get("title", "")
        vol_match = re.search(r"\bVol(?:ume)?\s+(-?\d+)", title_attr, re.IGNORECASE)
        if not vol_match:
            continue
        volume_number = int(vol_match.group(1))
        chapter_id = extract_chapter_id(link.get("href", ""), title_attr, item.get("data-number", ""))
        if chapter_id:
            grouped.setdefault(volume_number, []).append(chapter_id)

    ranges: dict[int, tuple[str, str]] = {}
    for volume_number, chapters in grouped.items():
        ordered = sorted(set(chapters), key=chapter_sort_key)
        ranges[volume_number] = (ordered[0], ordered[-1])
    return ranges


def parse_reader_anilist_id(html: str) -> int:
    for script in BeautifulSoup(html, "html.parser").find_all("script"):
        text = script.get_text(strip=True)
        if not text.startswith("{") or "anilist_id" not in text:
            continue
        try:
            payload = json.loads(text)
        except json.JSONDecodeError:
            continue
        value = payload.get("anilist_id")
        try:
            return int(value)
        except (TypeError, ValueError):
            return 0
    return 0


def anilist_exact_match(title: str) -> int:
    query = """
    query ($search: String) {
      Media(search: $search, type: MANGA) {
        id
        title { romaji english native }
        synonyms
      }
    }
    """
    try:
        response = requests.post(
            "https://graphql.anilist.co",
            json={"query": query, "variables": {"search": title}},
            headers={"User-Agent": SCRIPT_VERSION},
            timeout=20,
        )
        response.raise_for_status()
        media = response.json().get("data", {}).get("Media")
    except Exception:
        return 0
    if not media:
        return 0
    candidates = [
        media.get("title", {}).get("romaji"),
        media.get("title", {}).get("english"),
        media.get("title", {}).get("native"),
        *(media.get("synonyms") or []),
    ]
    title_norm = title.casefold().strip()
    for candidate in candidates:
        if candidate and candidate.casefold().strip() == title_norm:
            return int(media["id"])
    return 0


def ingest_series(url: str, *, enrich_anilist: bool = True) -> tuple[dict[str, Any], dict[str, Any]]:
    client = MangaFireClient()
    started = time.perf_counter()
    page = client.get(url)
    meta = parse_series_page(page.text, page.final_url)
    mangafire_id = meta["mangafireId"]
    manga_hash = manga_hash_from_id(mangafire_id)

    en_volumes_fragment, volume_elapsed = fetch_volume_fragment(client, manga_hash, "en", page.final_url)
    en_chapters_fragment, chapter_elapsed = fetch_chapter_fragment(client, manga_hash, "en", page.final_url)
    volumes = parse_volume_list(en_volumes_fragment)
    ranges = parse_chapter_ranges(en_chapters_fragment)

    try:
        ja_volumes_fragment, _ = fetch_volume_fragment(client, manga_hash, "ja", page.final_url)
        ja_volumes = parse_volume_list(ja_volumes_fragment)
    except Exception:
        ja_volumes = {}

    anilist_id = 0
    first_volume_url = ""
    if volumes:
        first_number = min(volumes)
        first_volume_url = volumes[first_number].get("url", "")
    if first_volume_url:
        try:
            reader = client.get(first_volume_url)
            anilist_id = parse_reader_anilist_id(reader.text)
        except Exception:
            anilist_id = 0
    if not anilist_id and enrich_anilist:
        anilist_id = anilist_exact_match(meta["seriesTitle"])

    output_volumes: list[dict[str, Any]] = []
    for number in sorted(volumes):
        volume = volumes[number]
        cover_url = volume["coverUrl"]
        if is_placeholder_image(cover_url):
            ja_cover = ja_volumes.get(number, {}).get("coverUrl", "")
            if not is_placeholder_image(ja_cover):
                cover_url = ja_cover
            else:
                cover_url = "" if is_placeholder_image(cover_url) else cover_url
        start, end = ranges.get(number, ("", ""))
        output_volumes.append(
            {
                "number": number,
                "title": "",
                "synopsis": "",
                "coverUrl": cover_url,
                "chapterStart": str(start),
                "chapterEnd": str(end),
            }
        )

    series_id = slugify(meta["seriesTitle"])
    catalog = {
        "seriesId": series_id,
        "seriesTitle": meta["seriesTitle"],
        "seriesTitleAlt": meta["seriesTitleAlt"],
        "anilistId": anilist_id,
        "mangafireUrl": meta["mangafireUrl"],
        "mangafireId": mangafire_id,
        "author": meta["author"],
        "studio": meta["studio"],
        "genres": meta["genres"],
        "status": meta["status"],
        "publishedYearStart": meta["publishedYearStart"],
        "publishedYearEnd": meta["publishedYearEnd"],
        "mangazine": meta["mangazine"],
        "malScoreRaw": meta["malScoreRaw"],
        "scrapedAt": utc_now_iso(),
        "source": SCRIPT_VERSION,
        "notes": "",
        "synopsis": meta["synopsis"],
        "volumes": output_volumes,
    }
    metrics = {
        "url": url,
        "seriesId": series_id,
        "elapsedSeconds": time.perf_counter() - started,
        "pageFetchSeconds": page.elapsed_seconds,
        "volumeFetchSeconds": volume_elapsed,
        "chapterFetchSeconds": chapter_elapsed,
        "volumeCount": len(output_volumes),
        "coverCount": sum(1 for v in output_volumes if v["coverUrl"]),
        "rangeCount": sum(1 for v in output_volumes if v["chapterStart"] and v["chapterEnd"]),
        "anilistId": anilist_id,
    }
    return catalog, metrics


def discover_sitemap_urls(limit: int | None = None) -> list[str]:
    client = MangaFireClient()
    root = client.get(SITEMAP_URL)
    sitemaps = re.findall(r"<loc>(https://mangafire\.to/sitemap-list-\d+\.xml)</loc>", root.text)
    urls: list[str] = []
    for sitemap in sitemaps:
        text = client.get(sitemap).text
        urls.extend(re.findall(r"<loc>(https://mangafire\.to/manga/[^<]+)</loc>", text))
        if limit and len(urls) >= limit:
            return urls[:limit]
    return urls


def title_to_url_guess(title: str, urls: list[str]) -> str | None:
    target = slugify(title).replace("-and-", "-")
    aliases = {
        "frieren-beyond-journey-s-end": ["sousou-no-frieren"],
        "frieren-beyond-journeys-end": ["sousou-no-frieren"],
        "yotsuba": ["yotsuba", "yotsubato"],
        "yotsuba-and": ["yotsuba", "yotsubato"],
    }
    candidates = [target, *(aliases.get(target, []))]
    scored: list[tuple[int, str]] = []
    for url in urls:
        stem = manga_fire_id_from_url(url).rsplit(".", 1)[0]
        simple = slugify(stem)
        for candidate in candidates:
            if simple == candidate or simple.rstrip("e") == candidate.rstrip("e"):
                scored.append((0, url))
            elif simple.startswith(candidate) or candidate in simple:
                scored.append((1, url))
    if not scored:
        return None
    scored.sort(key=lambda item: (item[0], len(item[1]), item[1]))
    return scored[0][1]


def write_catalog(catalog: dict[str, Any]) -> Path:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUTPUT_DIR / f"{catalog['seriesId']}.json"
    path.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("urls", nargs="*", help="MangaFire series URLs to ingest")
    parser.add_argument("--titles", nargs="*", default=[], help="Series titles to resolve via sitemap")
    parser.add_argument("--discover-sitemap", action="store_true", help="Ingest URLs discovered from sitemap")
    parser.add_argument("--limit", type=int, default=None, help="Limit discovered/input URLs")
    parser.add_argument("--max-workers", type=int, default=4, help="Maximum series ingests in parallel")
    parser.add_argument("--no-anilist", action="store_true", help="Disable AniList enrichment fallback")
    parser.add_argument("--metrics-out", type=Path, default=None, help="Write per-series metrics JSON")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    urls = [u if u.startswith("http") else urljoin(BASE_URL, u) for u in args.urls]

    sitemap_urls: list[str] = []
    if args.discover_sitemap or args.titles:
        sitemap_urls = discover_sitemap_urls(args.limit if args.discover_sitemap and not args.titles else None)
    if args.discover_sitemap:
        urls.extend(sitemap_urls)
    for title in args.titles:
        resolved = title_to_url_guess(title, sitemap_urls)
        if not resolved:
            print(f"ERROR: could not resolve title from sitemap: {title}", file=sys.stderr)
            return 2
        urls.append(resolved)

    seen: set[str] = set()
    urls = [u for u in urls if not (u in seen or seen.add(u))]
    if args.limit:
        urls = urls[: args.limit]
    if not urls:
        print("ERROR: provide URLs, --titles, or --discover-sitemap", file=sys.stderr)
        return 2

    metrics: list[dict[str, Any]] = []
    failures: list[dict[str, str]] = []
    max_workers = max(1, min(4, args.max_workers))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        future_to_url = {
            executor.submit(ingest_series, url, enrich_anilist=not args.no_anilist): url
            for url in urls
        }
        for future in concurrent.futures.as_completed(future_to_url):
            url = future_to_url[future]
            try:
                catalog, series_metrics = future.result()
                path = write_catalog(catalog)
                series_metrics["outputPath"] = str(path.relative_to(REPO_ROOT))
                metrics.append(series_metrics)
                print(f"WROTE {path.relative_to(REPO_ROOT)} volumes={series_metrics['volumeCount']}")
            except Exception as exc:
                failures.append({"url": url, "error": str(exc)})
                print(f"FAILED {url}: {exc}", file=sys.stderr)

    if args.metrics_out:
        args.metrics_out.parent.mkdir(parents=True, exist_ok=True)
        args.metrics_out.write_text(
            json.dumps({"metrics": metrics, "failures": failures}, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    if failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
