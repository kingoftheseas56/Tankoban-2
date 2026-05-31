from normalize import normalize_title


def test_lowercases_and_strips_punctuation():
    assert normalize_title("Invincible: Compendium One!") == "invincible compendium one"


def test_collapses_whitespace():
    assert normalize_title("  The  Walking   Dead  ") == "the walking dead"


def test_strips_edition_noise_words():
    assert normalize_title("Saga Vol. 1 (TPB)") == "saga vol 1"


def test_empty_is_empty():
    assert normalize_title("") == ""
