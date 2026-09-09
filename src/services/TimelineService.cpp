#include "TimelineService.h"

#include "ScriptService.h"

namespace mezozoy {

std::vector<TimelineRow> TimelineService::rows(const Project& project) const {
    std::vector<TimelineRow> result;
    result.reserve(project.scenes.size());
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        const Scene& scene = project.scenes[index];
        TimelineRow row; row.sceneId = scene.id; row.number = static_cast<int>(index + 1); row.title = scene.title;
        row.minutes = ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats);
        for (const auto& event : project.timelineEvents) if (event.sceneId == scene.id) {
            row.act = event.act; row.episode = event.episode; row.storyTime = event.storyTime;
            if (event.durationMinutes > 0.0f) row.minutes = event.durationMinutes;
            break;
        }
        result.push_back(std::move(row));
    }
    return result;
}

bool TimelineService::move(ProjectDocument& document, int sceneId, int delta) const {
    const std::size_t index = document.sceneIndexById(sceneId);
    return index < document.project().scenes.size() && document.moveScene(index, delta);
}

TimelineEvent& TimelineService::ensureEvent(Project& project, int sceneId) const {
    for (auto& event : project.timelineEvents) if (event.sceneId == sceneId) return event;
    TimelineEvent event; event.id = project.nextId(); event.sceneId = sceneId; event.orderIndex = static_cast<int>(project.timelineEvents.size());
    for (const auto& scene : project.scenes) if (scene.id == sceneId) { event.title = scene.title; event.durationMinutes = static_cast<float>(ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats)); break; }
    project.timelineEvents.push_back(std::move(event));
    return project.timelineEvents.back();
}

}  // namespace mezozoy
