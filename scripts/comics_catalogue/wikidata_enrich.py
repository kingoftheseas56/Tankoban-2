"""Enrich a Western series with structured facts from Wikidata (free, no
signup). Pure extraction (pick_comic_entity, extract_fields) is unit-tested;
the network glue (enrich) is exercised by the harvester run. No API key.

See docs/superpowers/specs/2026-06-01-comics-western-richness-design.md (D3).
"""
import json
import re
import time
import urllib.parse
import urllib.request

_API = "https://www.wikidata.org/w/api.php"
_UA = "TankobanCatalogue/1.0 (comics catalogue enrichment; contact: local)"

# P31 (instance-of) targets. STRONG = unambiguously a comic; accepted on type
# alone. WEAK = comic-possible but also used for TV/prose (e.g. "limited
# series", "literary work") — accepted ONLY when the entity also carries a
# comic signal (a publisher or creator claim), so a same-named show/novel
# doesn't win. Tunable.
STRONG_TYPES = {
    "Q14406742",  # comic book series
    "Q1760610",   # comic book
    "Q725377",    # graphic novel
    "Q333291",    # comic strip
    "Q21198342",  # manga series
    "Q1004",      # comics
}
WEAK_TYPES = {
    "Q3297186",   # limited series (comics often use this; also TV)
    "Q7725634",   # literary work
}
# Properties that signal "this is a real comic record" + feed the richness fields.
_CREATOR_PROPS = ["P50", "P58", "P110"]   # author, screenwriter, illustrator
_SIGNAL_PROPS = _CREATOR_PROPS + ["P123", "P136"]  # + publisher, genre


def _get(params: dict) -> dict:
    qs = urllib.parse.urlencode({**params, "format": "json"})
    req = urllib.request.Request(_API + "?" + qs, headers={"User-Agent": _UA})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


def _claim_ids(entity: dict, prop: str) -> list:
    out = []
    for c in entity.get("claims", {}).get(prop, []):
        try:
            out.append(c["mainsnak"]["datavalue"]["value"]["id"])
        except (KeyError, TypeError):
            pass
    return out


def _claim_year(entity: dict, prop: str) -> int:
    for c in entity.get("claims", {}).get(prop, []):
        try:
            t = c["mainsnak"]["datavalue"]["value"]["time"]  # "+2003-01-01T..."
            m = re.search(r"([+-]?\d{4})", t)
            if m:
                return abs(int(m.group(1)))
        except (KeyError, TypeError):
            pass
    return 0


def pick_comic_entity(candidates: list) -> str:
    """Disambiguate to the best comic entity. `candidates` are in search-rank
    order; each is {id, instance_of:[Qid,...], signals:int}. A STRONG type wins
    on type alone; a WEAK type qualifies only with signals>0. Among the
    eligible, prefer (strong over weak, then most signals, then earliest rank).
    Returns the chosen Qid, or None if nothing qualifies."""
    def tier(c) -> int:
        io = set(c.get("instance_of", []))
        if io & STRONG_TYPES:
            return 2
        if (io & WEAK_TYPES) and c.get("signals", 0) > 0:
            return 1
        return 0

    ranked = []
    for i, c in enumerate(candidates):
        t = tier(c)
        if t > 0:
            # higher tier, then more signals, then earlier rank (-i largest)
            ranked.append((t, c.get("signals", 0), -i, c["id"]))
    if not ranked:
        return None
    ranked.sort(reverse=True)
    return ranked[0][3]


def extract_fields(entity: dict, labels: dict) -> dict:
    """Pure: map an entity's claims + a {Qid: label} map to richness fields."""
    creators = []
    for prop in _CREATOR_PROPS:
        for qid in _claim_ids(entity, prop):
            name = labels.get(qid)
            if name and name not in creators:
                creators.append(name)
    publisher_ids = _claim_ids(entity, "P123")
    genre_ids = _claim_ids(entity, "P136")
    year = _claim_year(entity, "P577") or _claim_year(entity, "P571")
    return {
        "author": ", ".join(creators),
        "publisher": next((labels[q] for q in publisher_ids if q in labels), ""),
        "genres": [labels[q] for q in genre_ids if q in labels],
        "yearStart": year,
    }


_WIKI_API = "https://en.wikipedia.org/w/api.php"
# Disambiguation variants tried (in order) when resolving a comic's Wikidata
# QID via its Wikipedia article. Disambiguated forms first — bare title last,
# since common-word titles ("Saga", "Chew") resolve to non-comics otherwise.
_WIKI_VARIANTS = [" (comics)", " (comic book)", " (comic strip)", ""]


def _resolve_labels(refs: set) -> dict:
    """Resolve a set of Qids to their English labels ({} if none)."""
    if not refs:
        return {}
    time.sleep(0.5)
    lab = _get({"action": "wbgetentities", "ids": "|".join(sorted(refs)),
                "props": "labels", "languages": "en"})
    out = {}
    for qid, e in lab.get("entities", {}).items():
        out[qid] = e.get("labels", {}).get("en", {}).get("value", "")
    return out


def _fields_from_entity(entity: dict) -> dict:
    """Resolve referenced labels for an entity's claims, then extract fields."""
    refs = set()
    for prop in _CREATOR_PROPS + ["P123", "P136"]:
        refs.update(_claim_ids(entity, prop))
    return extract_fields(entity, _resolve_labels(refs))


def _is_comic_entity(entity: dict) -> bool:
    io = set(_claim_ids(entity, "P31"))
    if io & STRONG_TYPES:
        return True
    if io & WEAK_TYPES and sum(1 for p in _SIGNAL_PROPS if _claim_ids(entity, p)):
        return True
    return False


def _wiki_pageprops_qid(full_title: str) -> str:
    """Wikidata QID for one exact Wikipedia article title (follows redirects).
    '' on miss."""
    try:
        params = {"action": "query", "prop": "pageprops",
                  "ppprop": "wikibase_item", "redirects": "1",
                  "titles": full_title, "format": "json"}
        qs = urllib.parse.urlencode(params)
        req = urllib.request.Request(_WIKI_API + "?" + qs,
                                     headers={"User-Agent": _UA})
        with urllib.request.urlopen(req, timeout=15) as r:
            data = json.loads(r.read().decode("utf-8"))
        for _, p in data.get("query", {}).get("pages", {}).items():
            qid = p.get("pageprops", {}).get("wikibase_item")
            if qid:
                return qid
    except Exception:
        pass
    return ""


def _enrich_via_search(title: str) -> dict:
    """Fallback: Wikidata label search + type/signal disambiguation."""
    search = _get({"action": "wbsearchentities", "search": title,
                   "language": "en", "type": "item", "limit": 10})
    ids = [h["id"] for h in search.get("search", [])]
    if not ids:
        return {}
    time.sleep(0.5)
    ent = _get({"action": "wbgetentities", "ids": "|".join(ids),
                "props": "claims"})
    entities = ent.get("entities", {})
    cands = []
    for qid in ids:  # preserve search-rank order
        e = entities.get(qid)
        if not e:
            continue
        signals = sum(1 for p in _SIGNAL_PROPS if _claim_ids(e, p))
        cands.append({"id": qid, "instance_of": _claim_ids(e, "P31"),
                      "signals": signals})
    chosen = pick_comic_entity(cands)
    if not chosen:
        return {}
    return _fields_from_entity(entities[chosen])


def _entity_claims(qid: str) -> dict:
    time.sleep(0.3)
    ent = _get({"action": "wbgetentities", "ids": qid, "props": "claims"})
    return ent.get("entities", {}).get(qid) or {}


def enrich(title: str) -> dict:
    """Return richness fields for `title`. Primary path resolves the comic's
    Wikidata QID via its Wikipedia article: a disambiguated suffix ('(comics)',
    '(comic book)', '(comic strip)') IS Wikipedia's confirmation that the
    article is the comic, so that entity is trusted directly; the bare title is
    accepted only if it independently looks like a comic (avoids homonyms).
    Falls back to Wikidata label search. Returns {} on any miss (caller treats
    as 'no enrichment'; the catalogue still has the RCO synopsis)."""
    try:
        best = {}
        for suffix in _WIKI_VARIANTS:
            qid = _wiki_pageprops_qid(title + suffix)
            if not qid:
                continue
            entity = _entity_claims(qid)
            if not entity:
                continue
            trusted = suffix != ""  # disambiguated article -> Wikipedia says comic
            if trusted or _is_comic_entity(entity):
                best = _fields_from_entity(entity)
                break  # canonical comic article found; stop scanning variants
        if any(best.values()):
            return best
        # Wikipedia gave no usable fields -> Wikidata label search may do better.
        searched = _enrich_via_search(title)
        return searched if any(searched.values()) else best
    except Exception:
        return {}
