#pragma once

#include "Page.h"

#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mezozoy::ui {

enum class WorkspaceSplitAxis {
    Horizontal,
    Vertical
};

struct WorkspacePanelDefinition {
    std::wstring id;
    std::wstring title;
    Page* page = nullptr;
    int minimumWidth = 240;
    int minimumHeight = 180;
    bool allowMultiple = false;
};

struct WorkspacePlacement {
    std::wstring nodeId;
    std::wstring panelId;
    RECT bounds{};
};

// A workspace is deliberately represented as a split tree instead of a fixed
// collection of sidebars. The 0.9.7 line uses a single leaf, while future versions can
// expose Blender-style split/join and editor-type switching without replacing
// the page architecture again.
class WorkspaceLayout {
public:
    WorkspaceLayout();
    explicit WorkspaceLayout(std::wstring panelId);

    void reset(const std::wstring& panelId);
    bool split(const std::wstring& nodeId, WorkspaceSplitAxis axis, float ratio,
               const std::wstring& newPanelId, bool placeNewPanelAfter = true);
    bool setPanel(const std::wstring& nodeId, const std::wstring& panelId);
    bool remove(const std::wstring& nodeId);
    std::vector<WorkspacePlacement> arrange(const RECT& bounds) const;
    bool empty() const { return !root_; }

private:
    struct Node {
        enum class Kind { Panel, Split } kind = Kind::Panel;
        std::wstring id;
        std::wstring panelId;
        WorkspaceSplitAxis axis = WorkspaceSplitAxis::Horizontal;
        float ratio = 0.5f;
        std::unique_ptr<Node> first;
        std::unique_ptr<Node> second;
    };

    std::unique_ptr<Node> root_;
    unsigned int nextNodeId_ = 1;

    std::wstring makeNodeId();
    Node* find(Node* node, const std::wstring& id) const;
    bool removeFrom(std::unique_ptr<Node>& node, const std::wstring& id);
    void arrangeNode(const Node& node, const RECT& bounds, std::vector<WorkspacePlacement>& output) const;
};

class WorkspaceHost {
public:
    bool registerPanel(WorkspacePanelDefinition definition);
    bool showSingle(const std::wstring& panelId);
    bool applyLayout(WorkspaceLayout layout);
    void layout(int width, int height);
    void hideAll();
    void commitVisible();

    Page* activePage() const;
    const std::wstring& activePanelId() const { return activePanelId_; }
    const WorkspacePanelDefinition* panel(const std::wstring& panelId) const;
    WorkspaceLayout& currentLayout() { return layout_; }

private:
    std::map<std::wstring, WorkspacePanelDefinition> panels_;
    WorkspaceLayout layout_;
    std::wstring activePanelId_;
    std::vector<Page*> visiblePages_;
};

}  // namespace mezozoy::ui
