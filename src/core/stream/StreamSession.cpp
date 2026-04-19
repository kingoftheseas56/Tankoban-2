#include "StreamSession.h"

// All logic lives inline in the header — Session is a pure data struct
// with trivial helpers. The .cpp exists so CMake's MOC + unity-build
// plumbing stays symmetric with the rest of the stream-rebuild files
// (StreamPieceWaiter, StreamPrioritizer, StreamSeekClassifier all have
// matching .h/.cpp pairs) and so future P3+P5 state-transition helpers
// have a home that doesn't force a header recompile on every change.
