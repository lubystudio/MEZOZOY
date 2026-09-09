#include "Xml.h"

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace mezozoy::xml {

const Node* Node::child(const std::string& childName) const {
    for (const auto& item : children) if (item.name == childName) return &item;
    return nullptr;
}

std::vector<const Node*> Node::all(const std::string& childName) const {
    std::vector<const Node*> result;
    for (const auto& item : children) if (item.name == childName) result.push_back(&item);
    return result;
}

std::string Node::value(const std::string& childName, const std::string& fallback) const {
    const Node* item = child(childName);
    return item ? item->text : fallback;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& source) : source_(source) {}

    Node parseDocument() {
        skipMisc();
        Node result = parseElement();
        skipMisc();
        return result;
    }

private:
    const std::string& source_;
    std::size_t position_ = 0;

    bool starts(const char* value) const {
        const std::string token(value);
        return source_.compare(position_, token.size(), token) == 0;
    }

    void expect(char value) {
        if (position_ >= source_.size() || source_[position_] != value) throw std::runtime_error("invalid XML syntax");
        ++position_;
    }

    void skipWhitespace() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_;
    }

    void skipUntil(const char* ending) {
        const std::size_t found = source_.find(ending, position_);
        if (found == std::string::npos) throw std::runtime_error("unterminated XML block");
        position_ = found + std::string(ending).size();
    }

    void skipMisc() {
        for (;;) {
            skipWhitespace();
            if (starts("<?")) { position_ += 2; skipUntil("?>"); continue; }
            if (starts("<!--")) { position_ += 4; skipUntil("-->"); continue; }
            if (starts("<!DOCTYPE")) { position_ += 9; skipUntil(">"); continue; }
            break;
        }
    }

    std::string parseName() {
        const std::size_t begin = position_;
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.')) break;
            ++position_;
        }
        if (begin == position_) throw std::runtime_error("missing XML element name");
        return source_.substr(begin, position_ - begin);
    }

    void skipAttribute() {
        parseName();
        skipWhitespace();
        expect('=');
        skipWhitespace();
        if (position_ >= source_.size() || (source_[position_] != '\'' && source_[position_] != '"')) throw std::runtime_error("invalid XML attribute");
        const char quote = source_[position_++];
        while (position_ < source_.size() && source_[position_] != quote) ++position_;
        expect(quote);
    }

    Node parseElement() {
        expect('<');
        Node node;
        node.name = parseName();
        for (;;) {
            skipWhitespace();
            if (starts("/>")) { position_ += 2; return node; }
            if (starts(">")) { ++position_; break; }
            skipAttribute();
        }

        std::string text;
        for (;;) {
            if (position_ >= source_.size()) throw std::runtime_error("unterminated XML element");
            if (starts("</")) {
                position_ += 2;
                const std::string closing = parseName();
                if (closing != node.name) throw std::runtime_error("mismatched XML element");
                skipWhitespace();
                expect('>');
                node.text += Decode(text);
                return node;
            }
            if (starts("<![CDATA[")) {
                position_ += 9;
                const std::size_t end = source_.find("]]>", position_);
                if (end == std::string::npos) throw std::runtime_error("unterminated CDATA");
                text.append(source_, position_, end - position_);
                position_ = end + 3;
                continue;
            }
            if (starts("<!--")) {
                position_ += 4;
                skipUntil("-->");
                continue;
            }
            if (source_[position_] == '<') {
                if (!text.empty()) { node.text += Decode(text); text.clear(); }
                node.children.push_back(parseElement());
                continue;
            }
            text.push_back(source_[position_++]);
        }
    }
};

void ReplaceAll(std::string& value, const std::string& from, const std::string& to) {
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

}  // namespace

bool Document::parse(const std::string& source, std::string* error) {
    try {
        Parser parser(source);
        root = parser.parseDocument();
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        root = {};
        return false;
    }
}

std::string Escape(const std::string& value) {
    std::string result = value;
    ReplaceAll(result, "&", "&amp;");
    ReplaceAll(result, "<", "&lt;");
    ReplaceAll(result, ">", "&gt;");
    ReplaceAll(result, "\"", "&quot;");
    ReplaceAll(result, "'", "&apos;");
    return result;
}

std::string Decode(const std::string& value) {
    std::string result = value;
    ReplaceAll(result, "&lt;", "<");
    ReplaceAll(result, "&gt;", ">");
    ReplaceAll(result, "&quot;", "\"");
    ReplaceAll(result, "&apos;", "'");
    ReplaceAll(result, "&amp;", "&");
    return result;
}

}  // namespace mezozoy::xml
