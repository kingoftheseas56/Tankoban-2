from edition_classify import edition_tier, is_collected


def test_tier_ranking_by_label():
    assert edition_tier("Invincible Compendium 1") == 0
    assert edition_tier("Immortal Hulk Omnibus") == 1
    assert edition_tier("_TPB 25 - The End of All Things") == 2
    assert edition_tier("Deluxe Edition 1") == 3


def test_single_issue_excluded():
    assert edition_tier("Issue #144") == 99
    assert is_collected("Issue #1") is False
    assert is_collected("Compendium One") is True


def test_vol_is_soft_collected():
    # 'Vol' is ambiguous; treated as a low-confidence collected tier, not excluded.
    assert edition_tier("Vol 1") == 4
    assert is_collected("Vol 1") is True
    # ...but a bare-issue masquerading with a Vol prefix + issue marker stays excluded.
    assert is_collected("Vol 2 Issue #5") is False
