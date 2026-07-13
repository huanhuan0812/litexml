// xml_serializer.h
#ifndef LITEXML_XML_SERIALIZER_H
#define LITEXML_XML_SERIALIZER_H

#include "dom_node.h"
#include <string>
#include <string_view>
#include <algorithm>

namespace litexml {

class XMLSerializer {
public:
    struct Config {
        bool prettyPrint{false};
        std::string indentString{"  "}; 
        bool writeProlog{true};         
    };

    // 【修复】：拆分为默认构造和带参构造，解决 Clang 的 {} 解析限制
    XMLSerializer() = default;
    explicit XMLSerializer(const Config& config) : m_config(config) {}

    [[nodiscard]] std::string serialize(const DOMNode* node) const {
        std::string result;
        result.reserve(4096); 
        
        if (m_config.writeProlog && node->type == NodeType::Document) {
            auto* doc = static_cast<const DocumentNode*>(node);
            result += "<?xml version=\"";
            result += doc->getVersion().value_or("1.0");
            result += "\"";
            if (auto enc = doc->getEncoding(); enc.has_value()) {
                result += " encoding=\"";
                result += *enc;
                result += "\"";
            }
            result += "?>";
            if (m_config.prettyPrint) result += "\n";
        }

        serializeChildren(node, result, 0);
        return result;
    }

private:
    Config m_config;

    void serializeChildren(const DOMNode* node, std::string& out, int depth) const {
        for (const auto* child : node->children) {
            serializeNode(child, out, depth);
        }
    }

    void serializeNode(const DOMNode* node, std::string& out, int depth) const {
        if (m_config.prettyPrint && node->type != NodeType::Text && node->type != NodeType::CData) {
            for (int i = 0; i < depth; ++i) out += m_config.indentString;
        }

        switch (node->type) {
            case NodeType::Element: 
                serializeElement(static_cast<const ElementNode*>(node), out, depth); 
                break;
            case NodeType::Text: 
                serializeText(node->nodeValue, out); 
                break;
            case NodeType::CData: 
                serializeCData(node->nodeValue, out); 
                break;
            case NodeType::Comment: 
                serializeComment(node->nodeValue, out); 
                break;
            case NodeType::Document: 
                serializeChildren(node, out, depth); 
                break;
            default: 
                break;
        }

        if (m_config.prettyPrint && node->type != NodeType::Text && node->type != NodeType::CData) {
            out += "\n";
        }
    }

    void serializeElement(const ElementNode* elem, std::string& out, int depth) const {
        out += '<';
        out += elem->getTagName();

        for (const auto& [key, value] : elem->getAttributes()) {
            out += ' ';
            out += key;
            out += "=\"";
            out += escapeText(value, true); 
            out += '"';
        }

        if (elem->children.empty()) {
            out += "/>";
        } else {
            out += '>';
            
            bool hasElementChild = std::ranges::any_of(elem->children, [](const auto& c) { 
                return c->type == NodeType::Element; 
            });
            
            if (m_config.prettyPrint && hasElementChild) out += "\n";

            serializeChildren(elem, out, depth + 1);

            if (m_config.prettyPrint && hasElementChild) {
                for (int i = 0; i < depth; ++i) out += m_config.indentString;
            }
            out += "</";
            out += elem->getTagName();
            out += '>';
        }
    }

    void serializeText(std::string_view text, std::string& out) const {
        out += escapeText(text, false);
    }

    void serializeCData(std::string_view data, std::string& out) const {
        out += "<![CDATA[";
        out += data;
        out += "]]>";
    }

    void serializeComment(std::string_view comment, std::string& out) const {
        out += "<!--";
        out += comment;
        out += "-->";
    }

    static std::string escapeText(std::string_view text, bool isAttribute) {
        std::string result;
        result.reserve(text.size());
        for (char c : text) {
            switch (c) {
                case '&':  result += "&amp;"; break;
                case '<':  result += "&lt;"; break;
                case '>':  result += "&gt;"; break;
                case '"':  
                    if (isAttribute) result += "&quot;"; 
                    else result += c; 
                    break;
                case '\'': 
                    result += "&apos;"; 
                    break;
                default: 
                    result += c; 
                    break;
            }
        }
        return result;
    }
};

} // namespace litexml
#endif // LITEXML_XML_SERIALIZER_H