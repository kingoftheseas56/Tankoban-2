#pragma once

#include "VideoCategory.h"

struct ShowInfo;

class VideoClassifier {
public:
    VideoCategory classify(const ShowInfo& show) const;
};
