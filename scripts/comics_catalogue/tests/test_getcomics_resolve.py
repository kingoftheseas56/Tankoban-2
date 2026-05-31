import pathlib
from getcomics_resolve import extract_downloads, pick_best

FIX = pathlib.Path(__file__).parent / "fixtures" / "getcomics_post_capamerica.html"


def test_extracts_magnet_and_ddl_from_real_post():
    dls = extract_downloads(FIX.read_text(encoding="utf-8", errors="replace"))
    kinds = {d["kind"] for d in dls}
    assert "magnet" in kinds
    assert "main_server" in kinds
    # every extracted url is either a magnet or a getcomics /dls/ link (no ads)
    for d in dls:
        assert d["url"].startswith("magnet:") or "getcomics.org/dls/" in d["url"]


def test_magnet_url_is_real_magnet():
    dls = extract_downloads(FIX.read_text(encoding="utf-8", errors="replace"))
    mag = next(d for d in dls if d["kind"] == "magnet")
    assert mag["url"].startswith("magnet:?xt=urn:btih:")


def test_pick_best_prefers_magnet():
    dls = [{"kind": "main_server", "url": "https://getcomics.org/dls/x"},
           {"kind": "magnet", "url": "magnet:?xt=urn:btih:abc"}]
    assert pick_best(dls)["kind"] == "magnet"


def test_pick_best_falls_back_to_main_server_when_no_magnet():
    dls = [{"kind": "mega", "url": "https://getcomics.org/dls/m"},
           {"kind": "main_server", "url": "https://getcomics.org/dls/x"}]
    assert pick_best(dls)["kind"] == "main_server"


def test_pick_best_none_on_empty():
    assert pick_best([]) is None
