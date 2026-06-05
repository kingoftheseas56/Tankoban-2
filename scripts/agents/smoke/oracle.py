# scripts/agents/smoke/oracle.py
"""Pure-logic evaluator: does a probed VALUE satisfy an EXPECT spec?
Returns (passed: bool, reason: str). No I/O, no Qt, no tankoctl."""
import re


def _num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def evaluate(expect, value):
    """expect is a dict with exactly one or more operator keys (AND-combined).
    Supported: equals, approx(+tolerance), gt, gte, lt, lte, contains, regex,
    exists, nonempty, len_gte."""
    if not isinstance(expect, dict) or not expect:
        return False, f"bad expect spec: {expect!r}"

    for op, target in expect.items():
        if op == "tolerance":
            continue  # consumed by approx
        if op == "equals":
            if value != target:
                return False, f"equals {target!r}: got {value!r}"
        elif op == "approx":
            tol = _num(expect.get("tolerance", 0)) or 0.0
            v, t = _num(value), _num(target)
            if v is None or t is None or abs(v - t) > tol:
                return False, f"approx {target}±{tol}: got {value!r}"
        elif op in ("gt", "gte", "lt", "lte"):
            v, t = _num(value), _num(target)
            if v is None or t is None:
                return False, f"{op} {target}: non-numeric {value!r}"
            ok = (op == "gt" and v > t) or (op == "gte" and v >= t) \
                or (op == "lt" and v < t) or (op == "lte" and v <= t)
            if not ok:
                return False, f"{op} {target}: got {value}"
        elif op == "contains":
            if target not in (value or ""):
                return False, f"contains {target!r}: got {str(value)[:80]!r}"
        elif op == "regex":
            if not re.search(target, str(value or "")):
                return False, f"regex {target!r}: no match in {str(value)[:80]!r}"
        elif op == "exists":
            present = value is not None
            if present != bool(target):
                return False, f"exists {target}: value is {value!r}"
        elif op == "nonempty":
            ne = bool(value)
            if ne != bool(target):
                return False, f"nonempty {target}: got {value!r}"
        elif op == "len_gte":
            n = len(value) if hasattr(value, "__len__") else -1
            if n < target:
                return False, f"len_gte {target}: len is {n}"
        else:
            return False, f"unknown operator {op!r}"
    return True, "ok"
