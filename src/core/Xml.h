#pragma once

#include <string>
#include <vector>

namespace mezozoy::xml {

struct Node {
    std::string name;
    std::string text;
    std::vector<Node> children;

    const Node* child(const std::string& childName) const;
    std::vector<const Node*> all(const std::string& childName) const;
    std::string value(const std::string& childName, const std::string& fallback = {}) const;
};

class Document {
public:
    Node root;
    bool parse(const std::string& source, std::string* error = nullptr);
};

std::string Escape(const std::string& value);
std::string Decode(const std::string& value);

}  // namespace mezozoy::xml
