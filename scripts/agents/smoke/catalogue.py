# scripts/agents/smoke/catalogue.py
"""Pure-logic loader+validator for the smoke catalogue JSON."""
import json
from dataclasses import dataclass, field
from typing import List, Optional


class ValidationError(Exception):
    pass


@dataclass
class Assertion:
    id: str
    lane: str
    probe: Optional[list] = None
    path: Optional[str] = None
    expect: Optional[dict] = None
    gemini_prompt: Optional[str] = None
    timeoutSec: int = 10
    on_fail: str = "continue"
    needs: List[str] = field(default_factory=list)


@dataclass
class Step:
    id: str
    action: Optional[list]
    asserts: List[Assertion]
    settleSec: float = 1.5


@dataclass
class Journey:
    id: str
    name: str
    steps: List[Step]


@dataclass
class Catalogue:
    journeys: List[Journey]


def load(path):
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    seen_ids = set()
    journeys = []
    if not raw.get("journeys"):
        raise ValidationError("catalogue has no journeys")
    for j in raw["journeys"]:
        steps = []
        for s in j.get("steps", []):
            asserts = []
            for a in s.get("asserts", []):
                aid = a.get("id")
                if not aid:
                    raise ValidationError(f"assertion missing id in step {s.get('id')}")
                if aid in seen_ids:
                    raise ValidationError(f"duplicate assertion id {aid}")
                seen_ids.add(aid)
                lane = a.get("lane")
                if lane == "SELF":
                    if not a.get("probe") or "expect" not in a:
                        raise ValidationError(f"SELF assertion {aid} needs probe+expect")
                elif lane == "VISUAL":
                    if not a.get("gemini_prompt"):
                        raise ValidationError(f"VISUAL assertion {aid} needs gemini_prompt")
                else:
                    raise ValidationError(f"assertion {aid} bad lane {lane!r}")
                asserts.append(Assertion(
                    id=aid, lane=lane, probe=a.get("probe"), path=a.get("path"),
                    expect=a.get("expect"), gemini_prompt=a.get("gemini_prompt"),
                    timeoutSec=a.get("timeoutSec", 10), on_fail=a.get("on_fail", "continue"),
                    needs=a.get("needs", [])))
            steps.append(Step(id=s["id"], action=s.get("action"),
                              asserts=asserts, settleSec=s.get("settleSec", 1.5)))
        journeys.append(Journey(id=j["id"], name=j.get("name", j["id"]), steps=steps))
    return Catalogue(journeys=journeys)
