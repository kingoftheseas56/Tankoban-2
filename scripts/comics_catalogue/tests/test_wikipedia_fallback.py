from wikipedia_fallback import needs_fallback


def test_needs_fallback_true_for_empty_or_thin():
    assert needs_fallback("") is True
    assert needs_fallback("Too short.") is True          # < 120 chars
    assert needs_fallback(None) is True


def test_needs_fallback_false_for_substantial():
    assert needs_fallback("x" * 130) is False
