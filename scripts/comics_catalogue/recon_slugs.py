"""One-shot recon: probe candidate RCO slugs and report collected-edition
counts WITHOUT writing catalogue files. Keeps the ones worth seeding into
harvest.py's _SEED list. Blockbuster-first (project_catalog_scope_top500).

Run from scripts/comics_catalogue/:  python recon_slugs.py
"""
import sys
import time

from harvest import harvest_series

# (series_id, display_title, rco_slug) — wide net; recon filters to resolvers.
CANDIDATES = [
    ("the-walking-dead", "The Walking Dead", "The-Walking-Dead"),
    ("saga", "Saga", "Saga"),
    ("spawn", "Spawn", "Spawn"),
    ("the-boys", "The Boys", "The-Boys"),
    ("preacher", "Preacher", "Preacher"),
    ("y-the-last-man", "Y: The Last Man", "Y-The-Last-Man"),
    ("fables", "Fables", "Fables"),
    ("the-sandman", "The Sandman", "The-Sandman"),
    ("hellboy", "Hellboy", "Hellboy"),
    ("sin-city", "Sin City", "Sin-City"),
    ("watchmen", "Watchmen", "Watchmen"),
    ("v-for-vendetta", "V for Vendetta", "V-for-Vendetta"),
    ("kick-ass", "Kick-Ass", "Kick-Ass"),
    ("locke-and-key", "Locke and Key", "Locke-and-Key"),
    ("paper-girls", "Paper Girls", "Paper-Girls"),
    ("monstress", "Monstress", "Monstress"),
    ("east-of-west", "East of West", "East-of-West"),
    ("deadly-class", "Deadly Class", "Deadly-Class"),
    ("chew", "Chew", "Chew"),
    ("sweet-tooth", "Sweet Tooth", "Sweet-Tooth"),
    ("outcast", "Outcast", "Outcast"),
    ("descender", "Descender", "Descender"),
    ("black-hammer", "Black Hammer", "Black-Hammer"),
    ("the-wicked-the-divine", "The Wicked + The Divine", "The-Wicked-The-Divine"),
    ("bone", "Bone", "Bone"),
    ("transmetropolitan", "Transmetropolitan", "Transmetropolitan"),
    ("100-bullets", "100 Bullets", "100-Bullets"),
    ("scalped", "Scalped", "Scalped"),
    ("nailbiter", "Nailbiter", "Nailbiter"),
    ("southern-bastards", "Southern Bastards", "Southern-Bastards"),
    ("the-umbrella-academy", "The Umbrella Academy", "The-Umbrella-Academy"),
    ("daytripper", "Daytripper", "Daytripper"),
    ("we-stand-on-guard", "We Stand on Guard", "We-Stand-on-Guard"),
    ("seven-to-eternity", "Seven to Eternity", "Seven-to-Eternity"),
    ("gideon-falls", "Gideon Falls", "Gideon-Falls"),
]


def main():
    keep = []
    for sid, title, slug in CANDIDATES:
        try:
            rec = harvest_series(sid, title, slug)
            n = len(rec["editions"])
            cover = "cover" if rec.get("seriesCover") else "NO-COVER"
            status = "KEEP" if n >= 2 else "thin"
            print(f"[{status}] {slug:28} editions={n:3}  {cover}")
            if n >= 2:
                keep.append((sid, title, slug))
        except Exception as e:
            print(f"[FAIL] {slug:28} {type(e).__name__}: {e}", file=sys.stderr)
        time.sleep(1.0)  # polite

    print("\n=== _SEED candidates (>=2 editions) ===")
    for sid, title, slug in keep:
        print(f'    ("{sid}", "{title}", "{slug}"),')
    print(f"\n{len(keep)}/{len(CANDIDATES)} resolved with collected editions.")


if __name__ == "__main__":
    main()
