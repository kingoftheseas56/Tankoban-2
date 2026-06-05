"""Schema-v3 Western catalogue record assembly (GCD + Open Library brain).

Replaces the RCO harvest's schema-v2 record. Editions are now per-volume,
FORWARD-ordered (Vol 1 = the first TPB), each carrying its own ISBN + OL cover +
year. The C++ WesternCatalogLoader reads this on the source=="gcd" branch.
"""


def build_gcd_record(series_id, series_title, volumes, gcd_series_id=0,
                     synopsis="", author="", publisher="", genres=None,
                     year_start=0, year_end=0, status=""):
    """Assemble a schema-v3 GCD catalogue record. `volumes` are forward-ordered
    dicts each with volumeNumber/title/isbn/year/coverUrl. Every edition is a TPB
    (formatTier 2). seriesCover = first volume with a non-empty cover."""
    editions = [{
        "volumeNumber": v["volumeNumber"],
        "title": v["title"],
        "isbn": v["isbn"],
        "coverUrl": v.get("coverUrl", ""),
        "year": v.get("year", 0),
        "formatTier": 2,
        "tierLabel": "TPB",
    } for v in volumes]
    hero = next((e["coverUrl"] for e in editions if e["coverUrl"]), "")
    return {
        "seriesId": series_id,
        "seriesTitle": series_title,
        "source": "gcd",
        "schemaVersion": 3,
        "gcdSeriesId": gcd_series_id,
        "seriesCover": hero,
        "synopsis": synopsis,
        "author": author,
        "publisher": publisher,
        "genres": genres or [],
        "yearStart": year_start,
        "yearEnd": year_end,
        "status": status,
        "editions": editions,
    }
