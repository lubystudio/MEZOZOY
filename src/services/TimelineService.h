#pragma once

#include "ProjectDocument.h"

#include <string>
#include <vector>

namespace mezozoy {

struct TimelineRow {
    int sceneId = 0;
    int number = 0;
    std::wstring title;
    std::wstring act;
    std::wstring episode;
    std::wstring storyTime;
    double minutes = 0.0;
};

class TimelineService {
public:
    std::vector<TimelineRow> rows(const Project& project) const;
    bool move(ProjectDocument& document, int sceneId, int delta) const;
    TimelineEvent& ensureEvent(Project& project, int sceneId) const;
};

}  // namespace mezozoy
