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


def _majority_boilerplate(non_empty):
    """Longest sentence-bounded prefix shared by a MAJORITY of the synopses.

    A strict all-volumes common prefix is fragile: one volume with a different
    opening (e.g. Bleach Vol 74's "Part-time student, full-time Soul Reaper...")
    collapses the common prefix to nothing and disables stripping for the whole
    series. Instead, take each volume's sentence-bounded openings as candidates
    and keep the longest one that >=60% of volumes share. Tolerates outliers
    while still only stripping a genuinely repeated marketing intro.
    """
    n = len(non_empty)
    if n < 2:
        return ""
    threshold = max(2, (n * 6 + 9) // 10)  # ceil(0.6 * n), at least 2
    best = ""
    for cand_src in non_empty:
        bounds = [m.end() for m in re.finditer(r"[.!?]\s", cand_src)]
        for end in reversed(bounds):  # longest sentence-prefix of this candidate first
            prefix = cand_src[:end].rstrip()
            if len(prefix) < 30 or len(prefix) <= len(best):
                continue
            shared = sum(1 for s in non_empty if s.startswith(prefix))
            if shared >= threshold:
                best = prefix
                break
    return best


def strip_shared_prefixes(synopses, min_share=3, min_len=40):
    """Strip, per volume, the longest sentence-bounded opening shared by >=
    min_share volumes.

    Generalizes strip_series_boilerplate to SUB-MAJORITY boilerplate clusters:
    a generic series blurb prepended to only a RANGE of volumes (e.g. Bleach
    vols 48-74 all open with the same ~470-char "Part-time student, full-time
    Soul Reaper..." intro before their real per-volume text) is 36% of the
    series -- under the >=60% majority test, so strip_series_boilerplate misses
    it. A 40+ char sentence-bounded opening shared by >=3 volumes is boilerplate,
    not coincidence; strip it from each volume that carries it, exposing the real
    per-volume tail. Never blanks a volume that is pure-boilerplate (keeps it for
    the gate to flag as a gap).
    """
    non_empty = [s for s in synopses if s and s.strip()]
    if len(non_empty) < min_share:
        return list(synopses)
    out = []
    for s in synopses:
        if not s:
            out.append(s)
            continue
        bounds = [m.end() for m in re.finditer(r"[.!?]\s", s)]
        best = ""
        for end in reversed(bounds):  # longest sentence-prefix first
            prefix = s[:end].rstrip()
            if len(prefix) < min_len:
                break
            if sum(1 for o in non_empty if o.startswith(prefix)) >= min_share:
                best = prefix
                break
        if best:
            stripped = s[len(best):].lstrip(" -:").strip()
            out.append(stripped if stripped else s)
        else:
            out.append(s)
    return out


def strip_series_boilerplate(synopses):
    """Remove the leading marketing intro shared by a majority of the volumes.

    Returns a new list, same order. Only strips a substantial sentence-bounded
    intro (>= 30 chars) shared by >=60% of volumes, so unrelated short overlaps
    are left alone and an outlier volume can't disable stripping for the rest.
    """
    non_empty = [s for s in synopses if s and s.strip()]
    if len(non_empty) < 2:
        return list(synopses)
    boiler = _majority_boilerplate(non_empty)
    if len(boiler) < 30:
        return list(synopses)
    out = []
    for s in synopses:
        if s and s.startswith(boiler):
            stripped = s[len(boiler):].lstrip(" -:").strip()
            out.append(stripped if stripped else s)  # never blank a volume out
        else:
            out.append(s)
    return out
