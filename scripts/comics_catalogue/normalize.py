"""Title normalization for the Western comics catalogue.

Used to match RCO edition labels against GetComics post titles, and to key
series records. Mirrors the manga harvester's normalize step.
"""
import re

_PUNCT = re.compile(r"[^\w\s]", re.UNICODE)
_WS = re.compile(r"\s+")
_NOISE = re.compile(r"\b(tpb|hc|hardcover|paperback)\b", re.IGNORECASE)


def normalize_title(title: str) -> str:
    if not title:
        return ""
    # Punctuation (incl. ':' '(' ')' '.') -> space; do NOT truncate at the colon
    # (an edition title like "Invincible: Compendium One" must keep both halves).
    t = _PUNCT.sub(" ", title)
    t = _NOISE.sub(" ", t)
    t = _WS.sub(" ", t).strip().lower()
    return t
