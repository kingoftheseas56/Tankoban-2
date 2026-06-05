# scripts/agents/smoke/probes.py
"""Pure-logic accessor: pull a value out of a parsed tankoctl reply by a
dotted/indexed path. Supports `a.b`, `a[0].b`, and `a[*].b` (collect)."""
import re

MISSING = object()
_TOK = re.compile(r"([^.\[\]]+)|\[(\d+|\*)\]")


def _tokens(path):
    out = []
    for name, idx in _TOK.findall(path):
        if name:
            out.append(("key", name))
        elif idx == "*":
            out.append(("all", None))
        else:
            out.append(("idx", int(idx)))
    return out


def extract(obj, path):
    """Return the value at `path`, or MISSING if any hop is absent."""
    cur = obj
    for kind, val in _tokens(path):
        if kind == "key":
            if isinstance(cur, dict) and val in cur:
                cur = cur[val]
            else:
                return MISSING
        elif kind == "idx":
            if isinstance(cur, list) and -len(cur) <= val < len(cur):
                cur = cur[val]
            else:
                return MISSING
        elif kind == "all":
            if not isinstance(cur, list):
                return MISSING
            # remaining tokens after [*] apply to each element
            rest = path.split("[*]", 1)[1].lstrip(".")
            return [extract(e, rest) for e in cur] if rest else cur
    return cur
