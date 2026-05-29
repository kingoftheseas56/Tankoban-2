# scripts/synopsis_harvester/tests/conftest.py
from pathlib import Path
import pytest

@pytest.fixture
def fixtures_dir():
    return Path(__file__).parent / "fixtures"
