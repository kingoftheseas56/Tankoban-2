// src/core/manga/fandom/FandomTypes.cpp
//
// FandomReference, FandomVolume, FandomCatalog are Qt-value POD structs;
// no out-of-line definitions needed yet.
//
// JSON serialization (toJson / fromJson) lands in FandomCatalogCache.cpp at
// plan Task 12. Keeping it OUT of this file preserves a clean foundation:
// upstream extractors + downstream UI consumers only depend on the value
// shape, not on how it's cached.
//
// This .cpp exists so the foundational header has a translation unit
// associated in CMakeLists.txt — future cross-TU helpers (display-formatting
// hooks per Codex §4.3, hierarchy-flattening helpers) will accrete here.

#include "FandomTypes.h"

// Intentionally empty for Task 1.
