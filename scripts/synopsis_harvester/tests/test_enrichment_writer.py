# scripts/synopsis_harvester/tests/test_enrichment_writer.py
import json
from synopsis_harvester.enrichment_writer import build_enrichment

def test_build_enrichment_shape():
    vols = [
        {"volumeNumber": 4, "englishTitle": "", "englishReleaseDate": "2019-02-19",
         "synopsis": "The hit comedy manga ..."},
        {"volumeNumber": 5, "englishTitle": "", "englishReleaseDate": "",
         "synopsis": ""},
    ]
    doc = build_enrichment(series_id="grand-blue-dreaming", anilist_id=100568,
                           title="Grand Blue Dreaming", volumes=vols,
                           verified_date="2026-05-29")
    assert doc["seriesId"] == "grand-blue-dreaming"
    assert doc["anilistId"] == 100568
    assert doc["enrichmentBasis"] == "wikipedia_isbn_bn_harvest"
    assert doc["lastVerifiedDate"] == "2026-05-29"
    # Only volumes that actually got a synopsis are emitted.
    nums = [v["volumeNumber"] for v in doc["volumes"]]
    assert nums == [4]
    v = doc["volumes"][0]
    assert set(v.keys()) == {"volumeNumber", "englishTitle", "englishReleaseDate", "synopsis"}
    # round-trips as valid JSON
    json.loads(json.dumps(doc))
