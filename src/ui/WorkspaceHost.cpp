#include "WorkspaceHost.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace mezozoy::ui {

WorkspaceLayout::WorkspaceLayout() = default;

WorkspaceLayout::WorkspaceLayout(std::wstring panelId) {
    reset(panelId);
}

std::wstring WorkspaceLayout::makeNodeId() {
    return L"area-" + std::to_wstring(nextNodeId_++);
}

void WorkspaceLayout::reset(const std::wstring& panelId) {
    root_ = std::make_unique<Node>();
    root_->id = makeNodeId();
    root_->panelId = panelId;
}

WorkspaceLayout::Node* WorkspaceLayout::find(Node* node, const std::wstring& id) const {
    if (!node) return nullptr;
    if (node->id == id) return node;
    if (Node* found = find(node->first.get(), id)) return found;
    return find(node->second.get(), id);
}

bool WorkspaceLayout::split(const std::wstring& nodeId, WorkspaceSplitAxis axis, float ratio,
                            const std::wstring& newPanelId, bool placeNewPanelAfter) {
    Node* target = find(root_.get(), nodeId);
    if (!target || target->kind != Node::Kind::Panel || newPanelId.empty()) return false;

    auto existing = std::make_unique<Node>();
    existing->id = makeNodeId();
    existing->panelId = target->panelId;
    auto added = std::make_unique<Node>();
    added->id = makeNodeId();
    added->panelId = newPanelId;

    target->kind = Node::Kind::Split;
    target->panelId.clear();
    target->axis = axis;
    target->ratio = std::clamp(ratio, 0.12f, 0.88f);
    target->first = placeNewPanelAfter ? std::move(existing) : std::move(added);
    target->second = placeNewPanelAfter ? std::move(added) : std::move(existing);
    return true;
}

bool WorkspaceLayout::setPanel(const std::wstring& nodeId, const std::wstring& panelId) {
    Node* target = find(root_.get(), nodeId);
    if (!target || target->kind != Node::Kind::Panel || panelId.empty()) return false;
    target->panelId = panelId;
    return true;
}

bool WorkspaceLayout::removeFrom(std::unique_ptr<Node>& node, const std::wstring& id) {
    if (!node || node->kind != Node::Kind::Split) return false;
    if (node->first && node->first->id == id) {
        node = std::move(node->second);
        return true;
    }
    if (node->second && node->second->id == id) {
        node = std::move(node->first);
        return true;
    }
    return removeFrom(node->first, id) || removeFrom(node->second, id);
}

bool WorkspaceLayout::remove(const std::wstring& nodeId) {
    if (!root_) return false;
    if (root_->id == nodeId) {
        root_.reset();
        return true;
    }
    return removeFrom(root_, nodeId);
}

void WorkspaceLayout::arrangeNode(const Node& node, const RECT& bounds,
                                  std::vector<WorkspacePlacement>& output) const {
    if (node.kind == Node::Kind::Panel) {
        output.push_back({node.id, node.panelId, bounds});
        return;
    }
    if (!node.first || !node.second) return;

    RECT first = bounds;
    RECT second = bounds;
    if (node.axis == WorkspaceSplitAxis::Horizontal) {
        const int width = std::max(0L, bounds.right - bounds.left);
        const int splitX = bounds.left + static_cast<int>(std::lround(width * node.ratio));
        first.right = splitX;
        second.left = splitX;
    } else {
        const int height = std::max(0L, bounds.bottom - bounds.top);
        const int splitY = bounds.top + static_cast<int>(std::lround(height * node.ratio));
        first.bottom = splitY;
        second.top = splitY;
    }
    arrangeNode(*node.first, first, output);
    arrangeNode(*node.second, second, output);
}

std::vector<WorkspacePlacement> WorkspaceLayout::arrange(const RECT& bounds) const {
    std::vector<WorkspacePlacement> output;
    if (root_) arrangeNode(*root_, bounds, output);
    return output;
}

bool WorkspaceHost::registerPanel(WorkspacePanelDefinition definition) {
    if (definition.id.empty() || !definition.page || panels_.contains(definition.id)) return false;
    panels_.emplace(definition.id, std::move(definition));
    return true;
}

const WorkspacePanelDefinition* WorkspaceHost::panel(const std::wstring& panelId) const {
    const auto found = panels_.find(panelId);
    return found == panels_.end() ? nullptr : &found->second;
}

void WorkspaceHost::hideAll() {
    visiblePages_.clear();
    for (auto& [id, definition] : panels_) {
        (void)id;
        ShowWindow(definition.page->hwnd(), SW_HIDE);
    }
}

bool WorkspaceHost::showSingle(const std::wstring& panelId) {
    if (!panel(panelId)) return false;
    WorkspaceLayout single(panelId);
    activePanelId_ = panelId;
    return applyLayout(std::move(single));
}

bool WorkspaceHost::applyLayout(WorkspaceLayout layout) {
    hideAll();
    layout_ = std::move(layout);
    const RECT probe{0, 0, 1600, 900};
    for (const WorkspacePlacement& placement : layout_.arrange(probe)) {
        const WorkspacePanelDefinition* definition = panel(placement.panelId);
        if (!definition) continue;
        if (!definition->allowMultiple && std::find(visiblePages_.begin(), visiblePages_.end(), definition->page) != visiblePages_.end()) continue;
        visiblePages_.push_back(definition->page);
        ShowWindow(definition->page->hwnd(), SW_SHOW);
    }
    if (activePanelId_.empty() && !visiblePages_.empty()) {
        for (const auto& [id, definition] : panels_) if (definition.page == visiblePages_.front()) { activePanelId_ = id; break; }
    }
    return !visiblePages_.empty();
}

void WorkspaceHost::layout(int width, int height) {
    const RECT bounds{0, 0, std::max(0, width), std::max(0, height)};
    std::set<Page*> positioned;
    for (const WorkspacePlacement& placement : layout_.arrange(bounds)) {
        const WorkspacePanelDefinition* definition = panel(placement.panelId);
        if (!definition || !definition->page || positioned.contains(definition->page)) continue;
        positioned.insert(definition->page);
        const int panelWidth = std::max(0L, placement.bounds.right - placement.bounds.left);
        const int panelHeight = std::max(0L, placement.bounds.bottom - placement.bounds.top);
        definition->page->layoutAt(placement.bounds.left, placement.bounds.top, panelWidth, panelHeight);
    }
}

void WorkspaceHost::commitVisible() {
    for (Page* page : visiblePages_) if (page) page->commit();
}

Page* WorkspaceHost::activePage() const {
    const WorkspacePanelDefinition* definition = panel(activePanelId_);
    return definition ? definition->page : nullptr;
}

}  // namespace mezozoy::ui
