"""Clean harvested synopsis text.

Publisher/retailer descriptions prepend the same franchise marketing intro to
every volume ("The hit comedy manga comes to print by popular demand! ..."), and
glue the volume subtitle to the body without spacing. These helpers strip the
shared intro (so each volume leads with its own content) and tidy the spacing.
"""
import os
import re
import unicodedata

# CP1252 C1-control codepoints that leak into scraped text -> proper typography.
# Keyed by integer codepoint so no literal control char ever lives in source.
_C1_FIX = {
    0x85: "...",  # ellipsis
    0x91: "'", 0x92: "'",   # single quotes
    0x93: '"', 0x94: '"',   # double quotes
    0x96: "-", 0x97: "-",   # en / em dash
}


def normalize_text(s):
    """Fix mojibake, restore spacing lost when HTML blocks were concatenated."""
    if not s:
        return s
    s = "".join(_C1_FIX.get(ord(ch), ch) for ch in s)
    s = unicodedata.normalize("NFKC", s)
    s = re.sub(r"\s+", " ", s).strip()
    s = re.sub(r"([.!?])([A-Z])", r"\1 \2", s)            # "nudity!INTO" -> "nudity! INTO"
    s = re.sub(r"([A-Z]{3,})([A-Z][a-z])", r"\1: \2", s)  # "BLUEAfter" -> "BLUE: After"
    return s.strip()


def strip_series_boilerplate(synopses):
    """Remove the longest shared leading marketing intro across a series' volumes.

    Returns a new list, same order. Only strips when the shared prefix is a
    substantial sentence-bounded intro (>= 30 chars), so unrelated short overlaps
    are left alone.
    """
    non_empty = [s for s in synopses if s and s.strip()]
    if len(non_empty) < 2:
        return list(synopses)
    lcp = os.path.commonprefix(non_empty)
    # Trim the shared prefix back to its last sentence boundary so we never cut a word.
    m = re.search(r"^(.*[.!?])\s", lcp)
    boiler = m.group(1) if m else (lcp if len(lcp) >= 30 else "")
    if len(boiler) < 30:
        return list(synopses)
    out = []
    for s in synopses:
        if s and s.startswith(boiler):
            out.append(s[len(boiler):].lstrip(" -:").strip())
        else:
            out.append(s)
    return out
