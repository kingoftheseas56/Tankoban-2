import ol_covers


def test_cover_url_built_from_isbn():
    assert ol_covers.cover_url("9781582403201") == \
        "https://covers.openlibrary.org/b/isbn/9781582403201-L.jpg"


def test_cover_url_empty_for_empty_isbn():
    assert ol_covers.cover_url("") == ""


def test_verified_cover_returns_url_when_present(monkeypatch):
    monkeypatch.setattr(ol_covers, "_head_ok", lambda url: True)
    assert ol_covers.verified_cover_url("9781582403201").endswith("-L.jpg")


def test_verified_cover_empty_when_absent(monkeypatch):
    monkeypatch.setattr(ol_covers, "_head_ok", lambda url: False)
    assert ol_covers.verified_cover_url("9781582403201") == ""
