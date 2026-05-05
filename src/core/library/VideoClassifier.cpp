#include "VideoClassifier.h"

#include "core/VideosScanner.h"

VideoCategory VideoClassifier::classify(const ShowInfo& /*show*/) const
{
    // v1 intentionally does no automatic classification. New discoveries
    // start in Miscellaneous; future auto-classify work replaces only this body.
    return VideoCategory::Miscellaneous;
}
