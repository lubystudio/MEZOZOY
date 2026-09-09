#include "ProjectSerializer.h"

#include "../core/ModelText.h"
#include "../core/Utf.h"
#include "../core/Xml.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace mezozoy {
namespace {

std::wstring W(const xml::Node& node, const char* name, const wchar_t* fallback = L"") {
    return utf::FromUtf8(node.value(name, utf::ToUtf8(fallback)));
}

void Indent(std::ostringstream& output, int depth) {
    for (int i = 0; i < depth; ++i) output << "  ";
}

std::wstring XmlSafe(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        const unsigned int code = static_cast<unsigned int>(character);
        const bool permittedControl = character == L'\t' || character == L'\n' || character == L'\r';
        const bool permittedCharacter = code >= 0x20 && code != 0xfffe && code != 0xffff;
        if (permittedControl || permittedCharacter) result.push_back(character);
    }
    return result;
}

void Tag(std::ostringstream& output, int depth, const char* name, const std::wstring& value) {
    Indent(output, depth);
    const std::wstring safeValue = XmlSafe(value);
    if (safeValue.empty()) output << '<' << name << " />\n";
    else output << '<' << name << '>' << xml::Escape(utf::ToUtf8(safeValue)) << "</" << name << ">\n";
}

template <typename T>
void TagNumber(std::ostringstream& output, int depth, const char* name, T value) {
    Indent(output, depth);
    output << '<' << name << '>';
    if constexpr (std::is_floating_point_v<T>) {
        output << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
    } else {
        output << value;
    }
    output << "</" << name << ">\n";
}

void TagBool(std::ostringstream& output, int depth, const char* name, bool value) {
    Indent(output, depth);
    output << '<' << name << '>' << (value ? "true" : "false") << "</" << name << ">\n";
}

const xml::Node* Container(const xml::Node& root, const char* name) { return root.child(name); }

std::wstring JoinIds(const std::vector<int>& ids) {
    std::wstring result;
    for (const int id : ids) {
        if (!result.empty()) result += L",";
        result += std::to_wstring(id);
    }
    return result;
}

std::vector<int> ParseIds(const std::wstring& text) {
    std::vector<int> result;
    std::wstringstream stream(text);
    std::wstring token;
    while (std::getline(stream, token, L',')) {
        try { if (!token.empty()) result.push_back(std::stoi(token)); } catch (...) {}
    }
    return result;
}

void WriteNote(std::ostringstream& output, const SceneNote& item) {
    Indent(output, 4); output << "<SceneNote>\n";
    TagNumber(output, 5, "Id", item.id); Tag(output, 5, "Title", item.title); Tag(output, 5, "Text", item.text); Tag(output, 5, "ColorHex", item.colorHex);
    Indent(output, 4); output << "</SceneNote>\n";
}

void WriteBreakdown(std::ostringstream& output, const BreakdownItem& item) {
    Indent(output, 4); output << "<BreakdownItem>\n";
    TagNumber(output, 5, "Id", item.id); TagNumber(output, 5, "SceneId", item.sceneId); Tag(output, 5, "Category", BreakdownCategoryKey(item.category));
    Tag(output, 5, "Name", item.name); Tag(output, 5, "Description", item.description); Tag(output, 5, "AssignedTo", item.assignedTo);
    Tag(output, 5, "Status", item.status); Tag(output, 5, "ColorHex", item.colorHex);
    Indent(output, 4); output << "</BreakdownItem>\n";
}

void WriteReview(std::ostringstream& output, const ReviewComment& item) {
    Indent(output, 4); output << "<ReviewComment>\n";
    TagNumber(output, 5, "Id", item.id); TagNumber(output, 5, "SceneId", item.sceneId); Tag(output, 5, "Type", ReviewMarkTypeKey(item.type));
    TagNumber(output, 5, "StartIndex", item.startIndex); TagNumber(output, 5, "Length", item.length); Tag(output, 5, "SelectedText", item.selectedText);
    Tag(output, 5, "CommentText", item.commentText); Tag(output, 5, "AuthorName", item.authorName); Tag(output, 5, "ColorHex", item.colorHex);
    TagBool(output, 5, "Resolved", item.resolved); Tag(output, 5, "CreatedAt", item.createdAt);
    Indent(output, 4); output << "</ReviewComment>\n";
}

}  // namespace

int ProjectSerializer::integer(const std::string& value, int fallback) {
    try { return value.empty() ? fallback : std::stoi(value); } catch (...) { return fallback; }
}

float ProjectSerializer::number(const std::string& value, float fallback) {
    try { return value.empty() ? fallback : std::stof(value); } catch (...) { return fallback; }
}

bool ProjectSerializer::boolean(const std::string& value, bool fallback) {
    if (value == "true" || value == "True" || value == "1") return true;
    if (value == "false" || value == "False" || value == "0") return false;
    return fallback;
}

bool ProjectSerializer::load(const std::filesystem::path& path, Project& project, std::wstring* error) const {
    try {
        xml::Document document;
        std::string parseError;
        if (!document.parse(utf::ReadFile(path), &parseError)) {
            if (error) *error = L"Файл проекта поврежден: " + utf::FromUtf8(parseError);
            return false;
        }
        if (document.root.name != "MezozoyProject") {
            if (error) *error = L"Это не проект Mezozoy.";
            return false;
        }

        Project loaded;
        loaded.id = W(document.root, "Id"); loaded.title = W(document.root, "Title", L"Без названия");
        const std::wstring posterData = W(document.root, "PosterImageData");
        if (!posterData.empty()) loaded.posterImageData = std::make_shared<const std::wstring>(posterData);
        loaded.posterImageFormat = W(document.root, "PosterImageFormat");
        loaded.genre = W(document.root, "Genre"); loaded.author = W(document.root, "Author"); loaded.year = W(document.root, "Year");
        loaded.tagline = W(document.root, "Tagline"); loaded.subtitle = W(document.root, "Subtitle");
        loaded.projectType = W(document.root, "ProjectType"); loaded.draftLabel = W(document.root, "DraftLabel");
        loaded.contact = W(document.root, "Contact"); loaded.copyright = W(document.root, "Copyright");
        loaded.logline = W(document.root, "Logline"); loaded.synopsis = W(document.root, "Synopsis"); loaded.description = W(document.root, "Description");
        loaded.projectFormatVersion = W(document.root, "ProjectFormatVersion", L"0.8");
        loaded.appVersionCreated = W(document.root, "AppVersionCreated", L"0.8.1");
        loaded.appVersionLastSaved = W(document.root, "AppVersionLastSaved", loaded.appVersionCreated.c_str());
        loaded.lastSavedAt = W(document.root, "LastSavedAt");

        if (const auto* documents = Container(document.root, "Documents")) {
            for (const auto* node : documents->all("ProjectDocument")) {
                ProjectDocumentEntry item;
                item.id = integer(node->value("Id"), loaded.nextId()); item.parentId = integer(node->value("ParentId"));
                item.type = ParseProjectDocumentType(W(*node, "Type", L"Screenplay")); item.title = W(*node, "Title", L"Новый документ");
                item.orderIndex = integer(node->value("OrderIndex"), static_cast<int>(loaded.documents.size())); item.expanded = boolean(node->value("Expanded"), true);
                item.text = W(*node, "Text"); item.rtf = W(*node, "Rtf"); loaded.documents.push_back(std::move(item));
            }
        }

        if (const auto* scenes = Container(document.root, "Scenes")) {
            for (const auto* node : scenes->all("Scene")) {
                Scene item;
                item.id = integer(node->value("Id"), static_cast<int>(loaded.scenes.size() + 1)); item.documentId = integer(node->value("DocumentId"));
                item.title = W(*node, "Title"); item.heading = W(*node, "Heading", item.title.c_str()); item.legacyLocation = W(*node, "Location");
                item.legacyTimeOfDay = W(*node, "TimeOfDay"); item.summary = W(*node, "Summary"); item.summaryManual = boolean(node->value("SummaryManual"), false);
                item.text = W(*node, "Text"); item.rtf = W(*node, "Rtf"); item.screenplayFormats = W(*node, "ScreenplayFormats");
                item.orderIndex = integer(node->value("OrderIndex"), static_cast<int>(loaded.scenes.size()));
                item.cardX = number(node->value("CardX"), -1.0f); item.cardY = number(node->value("CardY"), -1.0f);
                item.titleAutoGenerated = boolean(node->value("TitleAutoGenerated"), false); item.locked = boolean(node->value("IsLocked"), false);
                item.boardColumn = W(*node, "BoardColumn"); item.status = W(*node, "Status", L"Черновик"); item.colorHex = W(*node, "ColorHex", L"#A56BFF");
                item.characterIds = ParseIds(W(*node, "CharacterIds")); item.locationIds = ParseIds(W(*node, "LocationIds"));
                item.createdAt = W(*node, "CreatedAt"); item.updatedAt = W(*node, "UpdatedAt");
                if (const auto* notes = node->child("Notes")) for (const auto* child : notes->all("SceneNote")) {
                    SceneNote note; note.id = integer(child->value("Id"), loaded.nextId()); note.title = W(*child, "Title", L"Заметка");
                    note.text = W(*child, "Text"); note.colorHex = W(*child, "ColorHex", L"#A56BFF"); item.notes.push_back(std::move(note));
                }
                if (const auto* breakdown = node->child("BreakdownItems")) for (const auto* child : breakdown->all("BreakdownItem")) {
                    BreakdownItem value; value.id = integer(child->value("Id"), loaded.nextId()); value.sceneId = integer(child->value("SceneId"), item.id);
                    value.category = ParseBreakdownCategory(W(*child, "Category", L"Note")); value.name = W(*child, "Name", L"Новый элемент");
                    value.description = W(*child, "Description"); value.assignedTo = W(*child, "AssignedTo"); value.status = W(*child, "Status", L"Не начато");
                    value.colorHex = W(*child, "ColorHex", L"#A56BFF"); item.breakdownItems.push_back(std::move(value));
                }
                if (const auto* reviews = node->child("ReviewComments")) for (const auto* child : reviews->all("ReviewComment")) {
                    ReviewComment value; value.id = integer(child->value("Id"), loaded.nextId()); value.sceneId = integer(child->value("SceneId"), item.id);
                    value.type = ParseReviewMarkType(W(*child, "Type", L"Comment")); value.startIndex = integer(child->value("StartIndex")); value.length = integer(child->value("Length"));
                    value.selectedText = W(*child, "SelectedText"); value.commentText = W(*child, "CommentText"); value.authorName = W(*child, "AuthorName");
                    value.colorHex = W(*child, "ColorHex", L"#A56BFF"); value.resolved = boolean(child->value("Resolved"), false); value.createdAt = W(*child, "CreatedAt");
                    item.reviewComments.push_back(std::move(value));
                }
                loaded.scenes.push_back(std::move(item));
            }
        }

        if (const auto* characters = Container(document.root, "Characters")) for (const auto* node : characters->all("Character")) {
            Character item; item.id = integer(node->value("Id"), loaded.nextId()); item.name = W(*node, "Name", L"Без имени"); item.realName = W(*node, "RealName");
            item.aliases = W(*node, "Aliases"); item.role = W(*node, "Role"); item.description = W(*node, "Description"); item.shortDescription = W(*node, "ShortDescription");
            item.age = W(*node, "Age"); item.appearance = W(*node, "Appearance"); item.personality = W(*node, "Personality"); item.motivation = W(*node, "Motivation");
            item.goal = W(*node, "Goal"); item.conflict = W(*node, "Conflict"); item.backstory = W(*node, "Backstory"); item.arc = W(*node, "Arc");
            item.colorHex = W(*node, "ColorHex", L"#A56BFF");
            const std::wstring avatarData = W(*node, "AvatarImageData");
            if (!avatarData.empty()) item.avatarImageData = std::make_shared<const std::wstring>(avatarData);
            item.avatarImageFormat = W(*node, "AvatarImageFormat"); item.avatarPath = W(*node, "AvatarPath");
            item.folderId = integer(node->value("FolderId"));
            if (const auto* profile = node->child("Profile")) for (const auto* field : profile->all("Field")) {
                const std::wstring key = W(*field, "Key");
                if (!key.empty()) item.profile[key] = W(*field, "Value");
            }
            item.archived = boolean(node->value("Archived"), false); loaded.characters.push_back(std::move(item));
        }

        if (const auto* locations = Container(document.root, "Locations")) for (const auto* node : locations->all("LocationItem")) {
            Location item; item.id = integer(node->value("Id"), loaded.nextId()); item.name = W(*node, "Name", L"Без названия"); item.type = W(*node, "Type");
            item.description = W(*node, "Description"); item.shortDescription = W(*node, "ShortDescription"); item.interiorExterior = W(*node, "InteriorExterior");
            item.timePeriod = W(*node, "TimePeriod"); item.visualMood = W(*node, "VisualMood"); item.notes = W(*node, "Notes");
            const std::wstring imageData = W(*node, "LocationImageData");
            if (!imageData.empty()) item.imageData = std::make_shared<const std::wstring>(imageData);
            item.imageFormat = W(*node, "LocationImageFormat"); item.imagePath = W(*node, "ImagePath");
            if (const auto* profile = node->child("Profile")) for (const auto* field : profile->all("Field")) {
                const std::wstring key = W(*field, "Key");
                if (!key.empty()) item.profile[key] = W(*field, "Value");
            }
            item.colorHex = W(*node, "ColorHex", L"#A56BFF"); item.folderId = integer(node->value("FolderId")); item.archived = boolean(node->value("Archived"), false);
            loaded.locations.push_back(std::move(item));
        }

        if (const auto* worlds = Container(document.root, "Worlds")) for (const auto* node : worlds->all("WorldItem")) {
            WorldItem item; item.id = integer(node->value("Id"), loaded.nextId()); item.name = W(*node, "Name", L"Элемент мира"); item.category = W(*node, "Category");
            item.description = W(*node, "Description"); item.rules = W(*node, "Rules"); item.notes = W(*node, "Notes"); item.linkedSceneIds = ParseIds(W(*node, "LinkedSceneIds"));
            loaded.worlds.push_back(std::move(item));
        }

        if (const auto* references = Container(document.root, "References")) for (const auto* node : references->all("ReferenceItem")) {
            ReferenceItem item; item.id = integer(node->value("Id"), loaded.nextId()); item.title = W(*node, "Title", L"Референс"); item.description = W(*node, "Description");
            const std::wstring imageData = W(*node, "ReferenceImageData");
            if (!imageData.empty()) item.imageData = std::make_shared<const std::wstring>(imageData);
            item.imageFormat = W(*node, "ReferenceImageFormat"); item.imagePath = W(*node, "ImagePath");
            item.aspectRatio = W(*node, "AspectRatio", L"16:9"); item.tags = W(*node, "Tags"); item.linkedSceneId = integer(node->value("LinkedSceneId"));
            item.linkedCharacterId = integer(node->value("LinkedCharacterId")); item.linkedLocationId = integer(node->value("LinkedLocationId")); loaded.references.push_back(std::move(item));
        }

        if (const auto* relations = Container(document.root, "CharacterRelations")) for (const auto* node : relations->all("RelationshipLink")) {
            RelationshipLink item; item.id = integer(node->value("Id"), loaded.nextId()); item.fromCharacterId = integer(node->value("FromCharacterId"));
            item.toCharacterId = integer(node->value("ToCharacterId")); item.relationType = W(*node, "RelationType"); item.description = W(*node, "Description");
            item.colorHex = W(*node, "ColorHex", L"#A56BFF"); loaded.characterRelations.push_back(std::move(item));
        }

        if (const auto* events = Container(document.root, "TimelineEvents")) for (const auto* node : events->all("TimelineEvent")) {
            TimelineEvent item; item.id = integer(node->value("Id"), loaded.nextId()); item.sceneId = integer(node->value("SceneId")); item.title = W(*node, "Title");
            item.act = W(*node, "Act"); item.episode = W(*node, "Episode"); item.storyTime = W(*node, "StoryTime"); item.orderIndex = integer(node->value("OrderIndex"));
            item.durationMinutes = number(node->value("DurationMinutes"), 0.0f); item.colorHex = W(*node, "ColorHex", L"#A56BFF"); loaded.timelineEvents.push_back(std::move(item));
        }

        if (const auto* sessions = Container(document.root, "ProductivitySessions")) for (const auto* node : sessions->all("ProductivitySession")) {
            ProductivitySession item; item.id = integer(node->value("Id"), loaded.nextId()); item.startedAt = W(*node, "StartedAt"); item.endedAt = W(*node, "EndedAt");
            item.wordsAdded = integer(node->value("WordsAdded")); item.wordsRemoved = integer(node->value("WordsRemoved")); item.documentId = integer(node->value("DocumentId"));
            item.sceneId = integer(node->value("SceneId")); loaded.productivitySessions.push_back(std::move(item));
        }

        if (const auto* folders = Container(document.root, "Folders")) for (const auto* node : folders->all("DevFolder")) {
            DevFolder item; item.id = integer(node->value("Id"), loaded.nextId()); item.name = W(*node, "Name", L"Новая папка");
            item.section = W(*node, "Section", L"character"); item.expanded = boolean(node->value("Expanded"), true); loaded.folders.push_back(std::move(item));
        }

        if (const auto* sections = Container(document.root, "BoardSections")) for (const auto* node : sections->all("BoardSection")) {
            BoardSection item; item.id = integer(node->value("Id"), loaded.nextId()); item.name = W(*node, "Name", L"Новый раздел"); item.mode = W(*node, "Mode", L"free");
            item.colorHex = W(*node, "ColorHex", L"#A56BFF"); item.x = number(node->value("X"), 80.0f); item.y = number(node->value("Y"), 80.0f);
            item.width = number(node->value("Width"), 980.0f); item.height = number(node->value("Height"), 420.0f); item.locked = boolean(node->value("IsLocked"), false);
            loaded.boardSections.push_back(std::move(item));
        }

        if (loaded.scenes.empty()) loaded.scenes = CreateDefaultProject().scenes;
        std::stable_sort(loaded.scenes.begin(), loaded.scenes.end(), [](const Scene& a, const Scene& b) { return a.orderIndex < b.orderIndex; });
        std::stable_sort(loaded.documents.begin(), loaded.documents.end(), [](const ProjectDocumentEntry& a, const ProjectDocumentEntry& b) { return a.orderIndex < b.orderIndex; });
        loaded.ensureDocumentStructure(); loaded.normalizeOrder(); project = std::move(loaded); return true;
    } catch (const std::exception& ex) {
        if (error) *error = L"Не удалось открыть проект: " + utf::FromUtf8(ex.what());
        return false;
    }
}

std::string ProjectSerializer::toXml(const Project& project) const {
    std::ostringstream output; output.imbue(std::locale::classic());
    output << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    output << "<MezozoyProject xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n";
    Tag(output, 1, "Id", project.id); Tag(output, 1, "Title", project.title);
    Tag(output, 1, "PosterImageData", project.posterImageData ? *project.posterImageData : std::wstring{});
    Tag(output, 1, "PosterImageFormat", project.posterImageFormat);
    Tag(output, 1, "Genre", project.genre); Tag(output, 1, "Author", project.author);
    Tag(output, 1, "Year", project.year); Tag(output, 1, "Tagline", project.tagline); Tag(output, 1, "Subtitle", project.subtitle);
    Tag(output, 1, "ProjectType", project.projectType); Tag(output, 1, "DraftLabel", project.draftLabel);
    Tag(output, 1, "Contact", project.contact); Tag(output, 1, "Copyright", project.copyright);
    Tag(output, 1, "Logline", project.logline); Tag(output, 1, "Synopsis", project.synopsis); Tag(output, 1, "Description", project.description);
    Tag(output, 1, "ProjectFormatVersion", project.projectFormatVersion); Tag(output, 1, "AppVersionCreated", project.appVersionCreated);
    Tag(output, 1, "AppVersionLastSaved", project.appVersionLastSaved); Tag(output, 1, "LastSavedAt", project.lastSavedAt);

    Indent(output, 1); output << "<Documents>\n";
    for (const auto& item : project.documents) {
        Indent(output, 2); output << "<ProjectDocument>\n"; TagNumber(output, 3, "Id", item.id); TagNumber(output, 3, "ParentId", item.parentId);
        Tag(output, 3, "Type", ProjectDocumentTypeKey(item.type)); Tag(output, 3, "Title", item.title); TagNumber(output, 3, "OrderIndex", item.orderIndex);
        TagBool(output, 3, "Expanded", item.expanded); Tag(output, 3, "Text", item.text); Tag(output, 3, "Rtf", item.rtf); Indent(output, 2); output << "</ProjectDocument>\n";
    }
    Indent(output, 1); output << "</Documents>\n";

    Indent(output, 1); output << "<Scenes>\n";
    for (const auto& item : project.scenes) {
        Indent(output, 2); output << "<Scene>\n"; TagNumber(output, 3, "Id", item.id); TagNumber(output, 3, "DocumentId", item.documentId);
        Tag(output, 3, "Title", item.title); Tag(output, 3, "Heading", item.heading); Tag(output, 3, "Location", item.legacyLocation); Tag(output, 3, "TimeOfDay", item.legacyTimeOfDay);
        Tag(output, 3, "Summary", item.summary); TagBool(output, 3, "SummaryManual", item.summaryManual); Tag(output, 3, "Text", item.text); Tag(output, 3, "Rtf", item.rtf);
        Tag(output, 3, "ScreenplayFormats", item.screenplayFormats);
        TagNumber(output, 3, "OrderIndex", item.orderIndex); TagNumber(output, 3, "CardX", item.cardX); TagNumber(output, 3, "CardY", item.cardY);
        TagBool(output, 3, "TitleAutoGenerated", item.titleAutoGenerated); TagBool(output, 3, "IsLocked", item.locked); Tag(output, 3, "BoardColumn", item.boardColumn);
        Tag(output, 3, "Status", item.status); Tag(output, 3, "ColorHex", item.colorHex); Tag(output, 3, "CharacterIds", JoinIds(item.characterIds));
        Tag(output, 3, "LocationIds", JoinIds(item.locationIds)); Tag(output, 3, "CreatedAt", item.createdAt); Tag(output, 3, "UpdatedAt", item.updatedAt);
        Indent(output, 3); output << "<Notes>\n"; for (const auto& note : item.notes) WriteNote(output, note); Indent(output, 3); output << "</Notes>\n";
        Indent(output, 3); output << "<BreakdownItems>\n"; for (const auto& value : item.breakdownItems) WriteBreakdown(output, value); Indent(output, 3); output << "</BreakdownItems>\n";
        Indent(output, 3); output << "<ReviewComments>\n"; for (const auto& value : item.reviewComments) WriteReview(output, value); Indent(output, 3); output << "</ReviewComments>\n";
        Indent(output, 2); output << "</Scene>\n";
    }
    Indent(output, 1); output << "</Scenes>\n";

    Indent(output, 1); output << "<Characters>\n";
    for (const auto& item : project.characters) {
        Indent(output, 2); output << "<Character>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "Name", item.name); Tag(output, 3, "RealName", item.realName);
        Tag(output, 3, "Aliases", item.aliases); Tag(output, 3, "Role", item.role); Tag(output, 3, "Description", item.description); Tag(output, 3, "ShortDescription", item.shortDescription);
        Tag(output, 3, "Age", item.age); Tag(output, 3, "Appearance", item.appearance); Tag(output, 3, "Personality", item.personality); Tag(output, 3, "Motivation", item.motivation);
        Tag(output, 3, "Goal", item.goal); Tag(output, 3, "Conflict", item.conflict); Tag(output, 3, "Backstory", item.backstory); Tag(output, 3, "Arc", item.arc);
        Tag(output, 3, "ColorHex", item.colorHex);
        Tag(output, 3, "AvatarImageData", item.avatarImageData ? *item.avatarImageData : std::wstring{});
        Tag(output, 3, "AvatarImageFormat", item.avatarImageFormat); Tag(output, 3, "AvatarPath", item.avatarPath);
        Indent(output, 3); output << "<Profile>\n";
        for (const auto& [key, value] : item.profile) {
            Indent(output, 4); output << "<Field>\n"; Tag(output, 5, "Key", key); Tag(output, 5, "Value", value); Indent(output, 4); output << "</Field>\n";
        }
        Indent(output, 3); output << "</Profile>\n";
        TagNumber(output, 3, "FolderId", item.folderId); TagBool(output, 3, "Archived", item.archived);
        Indent(output, 2); output << "</Character>\n";
    }
    Indent(output, 1); output << "</Characters>\n";

    Indent(output, 1); output << "<Locations>\n";
    for (const auto& item : project.locations) {
        Indent(output, 2); output << "<LocationItem>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "Name", item.name); Tag(output, 3, "Type", item.type);
        Tag(output, 3, "Description", item.description); Tag(output, 3, "ShortDescription", item.shortDescription); Tag(output, 3, "InteriorExterior", item.interiorExterior);
        Tag(output, 3, "TimePeriod", item.timePeriod); Tag(output, 3, "VisualMood", item.visualMood); Tag(output, 3, "Notes", item.notes);
        Tag(output, 3, "LocationImageData", item.imageData ? *item.imageData : std::wstring{});
        Tag(output, 3, "LocationImageFormat", item.imageFormat); Tag(output, 3, "ImagePath", item.imagePath);
        Indent(output, 3); output << "<Profile>\n";
        for (const auto& [key, value] : item.profile) {
            Indent(output, 4); output << "<Field>\n"; Tag(output, 5, "Key", key); Tag(output, 5, "Value", value); Indent(output, 4); output << "</Field>\n";
        }
        Indent(output, 3); output << "</Profile>\n";
        Tag(output, 3, "ColorHex", item.colorHex); TagNumber(output, 3, "FolderId", item.folderId); TagBool(output, 3, "Archived", item.archived); Indent(output, 2); output << "</LocationItem>\n";
    }
    Indent(output, 1); output << "</Locations>\n";

    Indent(output, 1); output << "<Worlds>\n";
    for (const auto& item : project.worlds) { Indent(output, 2); output << "<WorldItem>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "Name", item.name); Tag(output, 3, "Category", item.category); Tag(output, 3, "Description", item.description); Tag(output, 3, "Rules", item.rules); Tag(output, 3, "Notes", item.notes); Tag(output, 3, "LinkedSceneIds", JoinIds(item.linkedSceneIds)); Indent(output, 2); output << "</WorldItem>\n"; }
    Indent(output, 1); output << "</Worlds>\n";

    Indent(output, 1); output << "<References>\n";
    for (const auto& item : project.references) { Indent(output, 2); output << "<ReferenceItem>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "Title", item.title); Tag(output, 3, "Description", item.description); Tag(output, 3, "ReferenceImageData", item.imageData ? *item.imageData : std::wstring{}); Tag(output, 3, "ReferenceImageFormat", item.imageFormat); Tag(output, 3, "ImagePath", item.imagePath); Tag(output, 3, "AspectRatio", item.aspectRatio); Tag(output, 3, "Tags", item.tags); TagNumber(output, 3, "LinkedSceneId", item.linkedSceneId); TagNumber(output, 3, "LinkedCharacterId", item.linkedCharacterId); TagNumber(output, 3, "LinkedLocationId", item.linkedLocationId); Indent(output, 2); output << "</ReferenceItem>\n"; }
    Indent(output, 1); output << "</References>\n";

    Indent(output, 1); output << "<CharacterRelations>\n";
    for (const auto& item : project.characterRelations) { Indent(output, 2); output << "<RelationshipLink>\n"; TagNumber(output, 3, "Id", item.id); TagNumber(output, 3, "FromCharacterId", item.fromCharacterId); TagNumber(output, 3, "ToCharacterId", item.toCharacterId); Tag(output, 3, "RelationType", item.relationType); Tag(output, 3, "Description", item.description); Tag(output, 3, "ColorHex", item.colorHex); Indent(output, 2); output << "</RelationshipLink>\n"; }
    Indent(output, 1); output << "</CharacterRelations>\n";

    Indent(output, 1); output << "<TimelineEvents>\n";
    for (const auto& item : project.timelineEvents) { Indent(output, 2); output << "<TimelineEvent>\n"; TagNumber(output, 3, "Id", item.id); TagNumber(output, 3, "SceneId", item.sceneId); Tag(output, 3, "Title", item.title); Tag(output, 3, "Act", item.act); Tag(output, 3, "Episode", item.episode); Tag(output, 3, "StoryTime", item.storyTime); TagNumber(output, 3, "OrderIndex", item.orderIndex); TagNumber(output, 3, "DurationMinutes", item.durationMinutes); Tag(output, 3, "ColorHex", item.colorHex); Indent(output, 2); output << "</TimelineEvent>\n"; }
    Indent(output, 1); output << "</TimelineEvents>\n";

    Indent(output, 1); output << "<ProductivitySessions>\n";
    for (const auto& item : project.productivitySessions) { Indent(output, 2); output << "<ProductivitySession>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "StartedAt", item.startedAt); Tag(output, 3, "EndedAt", item.endedAt); TagNumber(output, 3, "WordsAdded", item.wordsAdded); TagNumber(output, 3, "WordsRemoved", item.wordsRemoved); TagNumber(output, 3, "DocumentId", item.documentId); TagNumber(output, 3, "SceneId", item.sceneId); Indent(output, 2); output << "</ProductivitySession>\n"; }
    Indent(output, 1); output << "</ProductivitySessions>\n";

    Indent(output, 1); output << "<Folders>\n";
    for (const auto& item : project.folders) { Indent(output, 2); output << "<DevFolder>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "Name", item.name); Tag(output, 3, "Section", item.section); TagBool(output, 3, "Expanded", item.expanded); Indent(output, 2); output << "</DevFolder>\n"; }
    Indent(output, 1); output << "</Folders>\n";

    Indent(output, 1); output << "<BoardSections>\n";
    for (const auto& item : project.boardSections) { Indent(output, 2); output << "<BoardSection>\n"; TagNumber(output, 3, "Id", item.id); Tag(output, 3, "Name", item.name); Tag(output, 3, "Mode", item.mode); Tag(output, 3, "ColorHex", item.colorHex); TagNumber(output, 3, "X", item.x); TagNumber(output, 3, "Y", item.y); TagNumber(output, 3, "Width", item.width); TagNumber(output, 3, "Height", item.height); TagBool(output, 3, "IsLocked", item.locked); Indent(output, 2); output << "</BoardSection>\n"; }
    Indent(output, 1); output << "</BoardSections>\n";
    output << "</MezozoyProject>\n"; return output.str();
}

bool ProjectSerializer::save(const std::filesystem::path& path, const Project& project, std::wstring* error) const {
    return utf::WriteFileAtomic(path, toXml(project), error);
}

}  // namespace mezozoy
