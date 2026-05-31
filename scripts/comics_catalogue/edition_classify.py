"""Classify an RCO edition label into a collected-edition tier.

This is the spec-§3 heuristic: RCO has no structured `format` field, so we
type editions by keyword-matching the uploader-typed label string. Accepted as
blockbuster-clean (famous-series labels are unambiguous); the `Vol.` ambiguity
only bites the obscure long tail, which is out of scope.

Tier: lower = stronger collected-edition signal. 99 = single issue / unknown
(excluded from the primary collected-edition tiles).
"""
import re

# Ordered by signal strength. First match wins.
_RULES = [
    (re.compile(r"\bcompendium\b", re.I), 0),
    (re.compile(r"\bomnibus\b", re.I), 1),
    (re.compile(r"\b(tpb|trade paperback|complete collection)\b", re.I), 2),
    (re.compile(r"\b(deluxe|absolute|library edition)\b", re.I), 3),
    (re.compile(r"\bvol\.?\b|\bvolume\b", re.I), 4),  # ambiguous, soft-collected
]
_ISSUE = re.compile(r"\bissue\b|#\s*\d", re.I)


def edition_tier(label: str) -> int:
    for rx, tier in _RULES:
        if rx.search(label):
            return tier
    return 99


def is_collected(label: str) -> bool:
    """True if the label denotes a collected edition we'd surface as a tile.

    Strong tiers (compendium/omnibus/tpb/deluxe) always count. The soft `Vol`
    tier counts only when no single-issue marker is present (guards against
    'Vol 2 Issue #5' = a single issue from a publishing run, not a collection).
    """
    t = edition_tier(label)
    if t <= 3:
        return True
    if t == 4 and not _ISSUE.search(label):
        return True
    return False
