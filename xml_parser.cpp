// xml_parser.cpp
#include "xml_parser.h"
#include "entity_table.h"
#include <fstream>
#include <cctype>
#include <algorithm>
#include <format>

namespace litexml {

// ==================== 构造函数 ====================
XMLParser::XMLParser(const Config& config) : m_config(config) {}
XMLParser::XMLParser() : m_config(Config{}) {}

// ==================== ParseState 方法 ====================
void XMLParser::ParseState::skipWhitespace() noexcept {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(current()))) {
        advance();
    }
}

std::optional<std::string_view> XMLParser::ParseState::parseName() noexcept {
    if (atEnd()) return std::nullopt;
    size_t start = position;
    char c = current();
    if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_') return std::nullopt;
    advance();
    while (!atEnd()) {
        c = current();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ':') {
            advance();
        } else {
            break;
        }
    }
    return input.substr(start, position - start);
}

std::optional<std::string_view> XMLParser::ParseState::parseQuotedString() noexcept {
    if (atEnd()) return std::nullopt;
    char quote = current();
    if (quote != '"' && quote != '\'') return std::nullopt;
    advance();
    size_t start = position;
    while (!atEnd() && current() != quote) {
        advance();
    }
    if (atEnd()) return std::nullopt;
    std::string_view result = input.substr(start, position - start);
    advance(); 
    return result;
}

std::optional<std::string_view> XMLParser::ParseState::parseUntil(char delimiter) noexcept {
    if (atEnd()) return std::nullopt;
    size_t start = position;
    while (!atEnd() && current() != delimiter) {
        advance();
    }
    if (atEnd()) return std::nullopt;
    return input.substr(start, position - start);
}

// ==================== 辅助函数 ====================
ParseResult XMLParser::makeError(ParseError error, std::string_view message, 
                                  std::optional<size_t> pos) const noexcept {
    return ParseResult{.error = error, .message = std::string(message), .position = pos};
}

inline bool XMLParser::isWhitespaceOnly(std::string_view text) const noexcept {
    return std::ranges::all_of(text, [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    });
}

bool XMLParser::isValidName(std::string_view name) const noexcept {
    if (name.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') return false;
    return std::ranges::all_of(name.substr(1), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ':';
    });
}

// ==================== 核心解析入口 ====================
ParseResultT<std::unique_ptr<DocumentNode>> XMLParser::parse(std::string_view xml) noexcept {
    auto doc = std::make_unique<DocumentNode>();
    return parseInternal(xml, std::move(doc));
}

ParseResultT<std::unique_ptr<DocumentNode>> XMLParser::parseFile(std::string_view filename) noexcept {
    try {
        std::string filenameStr(filename);
        std::ifstream file(filenameStr, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return std::unexpected(makeError(ParseError::FileNotFound, 
                std::format("Cannot open file: {}", filename)));
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        auto buffer = std::make_unique<std::string>(static_cast<size_t>(size), '\0');
        if (!file.read(buffer->data(), size)) {
            return std::unexpected(makeError(ParseError::IOError, "Failed to read file"));
        }
        
        auto doc = std::make_unique<DocumentNode>();
        doc->setOwnedBuffer(std::move(buffer));
        return parseInternal(*doc->getOwnedBuffer(), std::move(doc));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(ParseError::IOError, e.what()));
    }
}

ParseResultT<std::unique_ptr<DocumentNode>> XMLParser::parseInternal(std::string_view xml, 
                                                                      std::unique_ptr<DocumentNode> doc) noexcept {
    try {
        ParseState state(xml, m_config, doc.get());
        if (state.startsWith("\xEF\xBB\xBF")) state.advance(3);
        state.skipWhitespace();

        if (state.startsWith("<?xml")) {
            auto result = parseProlog(state);
            if (!result.isSuccess()) return std::unexpected(result);
        }

        auto result = parseContent(state, state.document);
        if (!result.isSuccess()) return std::unexpected(result);

        if (!state.tagStack.empty()) {
            return std::unexpected(makeError(ParseError::MismatchedTag,
                std::format("Unclosed tag: {}", state.tagStack.back()), state.position));
        }
        return std::move(doc);
    } catch (const std::exception& e) {
        return std::unexpected(makeError(ParseError::MalformedXML, e.what()));
    }
}

// ==================== 解析实现 ====================
ParseResult XMLParser::parseProlog(ParseState& state) noexcept {
    state.position += 5;
    state.skipWhitespace();
    while (!state.atEnd() && state.current() != '?') {
        state.skipWhitespace();
        if (state.current() == '?') break;
        auto optName = state.parseName();
        if (!optName) return makeError(ParseError::InvalidProlog, "Expected attribute name", state.position);
        state.skipWhitespace();
        if (state.current() != '=') return makeError(ParseError::InvalidProlog, "Expected '='", state.position);
        state.advance();
        state.skipWhitespace();
        auto optValue = state.parseQuotedString();
        if (!optValue) return makeError(ParseError::InvalidProlog, "Expected quoted attribute value", state.position);

        if (*optName == "version") state.document->setVersion(*optValue);
        else if (*optName == "encoding") state.document->setEncoding(*optValue);
        state.skipWhitespace();
    }
    if (state.current() == '?' && state.peek() == '>') {
        state.position += 2;
        return ParseResult{ParseError::Success, "", std::nullopt};
    }
    return makeError(ParseError::InvalidProlog, "Expected '?>'", state.position);
}

ParseResult XMLParser::parseContent(ParseState& state, DOMNode* parent) noexcept {
    while (!state.atEnd()) {
        if (state.current() == '<') {
            if (state.peek() == '/') {
                return ParseResult{ParseError::Success, "", std::nullopt};
            } else if (state.peek() == '!') {
                if (state.startsWith("<!--")) {
                    auto result = parseComment(state, parent);
                    if (!result.isSuccess()) return result;
                } else if (state.startsWith("<![CDATA[")) {
                    auto result = parseCDATA(state, parent);
                    if (!result.isSuccess()) return result;
                } else {
                    size_t end = state.input.find('>', state.position + 1);
                    if (end == std::string_view::npos) {
                        return makeError(ParseError::UnexpectedEOF, "Unclosed DOCTYPE", state.position);
                    }
                    state.position = end + 1;
                }
            } else if (state.peek() == '?') {
                if (m_config.parseProcessingInstructions) {
                    auto result = parseProcessingInstruction(state, parent);
                    if (!result.isSuccess()) return result;
                } else {
                    size_t end = state.input.find("?>", state.position + 2);
                    if (end == std::string_view::npos) {
                        return makeError(ParseError::UnexpectedEOF, "Unclosed PI", state.position);
                    }
                    state.position = end + 2;
                }
            } else {
                auto result = parseElement(state, parent);
                if (!result.isSuccess()) return result;
            }
        } else {
            auto result = parseTextOptimized(state, parent);
            if (!result.isSuccess()) return result;
        }
    }
    return ParseResult{ParseError::Success, "", std::nullopt};
}

// ==================== 优化后的文本解析 ====================
ParseResult XMLParser::parseTextOptimized(ParseState& state, DOMNode* parent) noexcept {
    size_t start = state.position;
    bool has_entity = false;
    
    while (!state.atEnd() && state.current() != '<') {
        if (state.current() == '&') {
            has_entity = true;
            break;
        }
        state.advance();
    }
    
    if (!has_entity) {
        state.position = start;
        while (!state.atEnd() && state.current() != '<') {
            state.advance();
        }
        
        if (start == state.position) {
            return ParseResult{ParseError::Success, "", std::nullopt};
        }
        
        std::string_view text = state.input.substr(start, state.position - start);
        
        if (!m_config.preserveWhitespace && isWhitespaceOnly(text)) {
            return ParseResult{ParseError::Success, "", std::nullopt};
        }
        
        auto textNode = state.document->getAllocator().create<TextNode>(text);
        parent->appendChild(textNode);
        return ParseResult{ParseError::Success, "", std::nullopt};
    }
    
    state.position = start;
    auto& accumulator = state.get_text_accumulator();
    
    while (!state.atEnd() && state.current() != '<') {
        if (state.current() == '&') {
            size_t entity_start = state.position;
            state.advance();
            
            while (!state.atEnd() && state.current() != ';' && state.current() != '<') {
                state.advance();
            }
            
            if (state.atEnd() || state.current() != ';') {
                state.position = entity_start + 1;
                accumulator.append('&');
                continue;
            }
            
            std::string_view entity = state.input.substr(entity_start, state.position - entity_start + 1);
            
            if (entity == "&amp;") accumulator.append('&');
            else if (entity == "&lt;") accumulator.append('<');
            else if (entity == "&gt;") accumulator.append('>');
            else if (entity == "&quot;") accumulator.append('"');
            else if (entity == "&apos;") accumulator.append('\'');
            else {
                accumulator.append(entity);
            }
            
            state.advance();
            continue;
        }
        
        size_t seg_start = state.position;
        while (!state.atEnd() && state.current() != '<' && state.current() != '&') {
            state.advance();
        }
        
        if (seg_start != state.position) {
            accumulator.append(state.input.substr(seg_start, state.position - seg_start));
        }
    }
    
    if (accumulator.empty()) {
        return ParseResult{ParseError::Success, "", std::nullopt};
    }
    
    std::string_view text = accumulator.finalize();
    
    if (!m_config.preserveWhitespace && isWhitespaceOnly(text)) {
        return ParseResult{ParseError::Success, "", std::nullopt};
    }
    
    auto textNode = state.document->getAllocator().create<TextNode>(text);
    parent->appendChild(textNode);
    
    return ParseResult{ParseError::Success, "", std::nullopt};
}

// ==================== 命名空间解析 ====================
std::optional<std::string_view> XMLParser::resolveNamespace(ParseState& state, 
                                                             ElementNode* element, 
                                                             DOMNode* parent,
                                                             std::string_view prefix) noexcept {
    (void)state; // 避免未使用警告
    // 1. 检查元素自身的命名空间声明
    for (const auto& ns : element->getNamespaceDeclarations()) {
        if (ns.prefix == prefix) {
            return ns.uri;
        }
    }

    
    // 2. 递归检查父节点链（从传入的 parent 开始）
    DOMNode* currentParent = parent;
    while (currentParent) {
        if (currentParent->type == NodeType::Element) {
            auto* parentElem = static_cast<ElementNode*>(currentParent);
            for (const auto& ns : parentElem->getNamespaceDeclarations()) {
                if (ns.prefix == prefix) {
                    return ns.uri;
                }
            }
        }
        currentParent = currentParent->getParent();
    }
    
    // 3. 检查 DOM 树中的父节点
    DOMNode* domParent = element->getParent();
    while (domParent) {
        if (domParent->type == NodeType::Element) {
            auto* parentElem = static_cast<ElementNode*>(domParent);
            for (const auto& ns : parentElem->getNamespaceDeclarations()) {
                if (ns.prefix == prefix) {
                    return ns.uri;
                }
            }
        }
        domParent = domParent->getParent();
    }
    
    return std::nullopt;
}

void XMLParser::resolveElementNamespace(ParseState& state, ElementNode* element, DOMNode* parent) noexcept {
    if (!m_config.parseNamespaces) {
        return;
    }
    
    auto prefix = element->getPrefix();
    
    if (prefix.has_value()) {
        auto uri = resolveNamespace(state, element, parent, prefix.value());
        if (uri.has_value()) {
            element->setNamespaceUri(uri.value());
        }
    } else {
        // 无前缀：检查默认命名空间
        // 首先检查元素自身的默认命名空间声明
        auto defaultNs = element->getDefaultNamespace();
        if (defaultNs.has_value()) {
            element->setNamespaceUri(defaultNs.value());
            return;
        }
        
        // 递归检查父节点链
        DOMNode* currentParent = parent;
        while (currentParent) {
            if (currentParent->type == NodeType::Element) {
                auto* parentElem = static_cast<ElementNode*>(currentParent);
                auto parentDefault = parentElem->getDefaultNamespace();
                if (parentDefault.has_value()) {
                    element->setNamespaceUri(parentDefault.value());
                    return;
                }
            }
            currentParent = currentParent->getParent();
        }
        
        // 检查 DOM 树中的父节点
        DOMNode* domParent = element->getParent();
        while (domParent) {
            if (domParent->type == NodeType::Element) {
                auto* parentElem = static_cast<ElementNode*>(domParent);
                auto parentDefault = parentElem->getDefaultNamespace();
                if (parentDefault.has_value()) {
                    element->setNamespaceUri(parentDefault.value());
                    return;
                }
            }
            domParent = domParent->getParent();
        }
    }
}

// ==================== 元素解析 ====================
ParseResult XMLParser::parseElement(ParseState& state, DOMNode* parent) noexcept {
    if (state.current() != '<') return makeError(ParseError::InvalidTag, "Expected '<'", state.position);
    state.advance();
    auto optTagName = state.parseName();
    if (!optTagName) return makeError(ParseError::InvalidTag, "Expected element name", state.position);

    std::string_view tagName = *optTagName;
    if (state.tagStack.size() >= m_config.maxDepth) {
        return makeError(ParseError::MalformedXML, 
            std::format("Maximum nesting depth {} exceeded", m_config.maxDepth));
    }

    auto element = state.document->getAllocator().create<ElementNode>(tagName);
    
    auto result = parseAttributes(state, element);
    if (!result.isSuccess()) return result;

    // 先添加到父节点，这样 getParent() 就能正确工作
    parent->appendChild(element);

    // 然后解析命名空间（此时 element->getParent() 返回正确的父节点）
    resolveElementNamespace(state, element, parent);

    if (state.current() == '/' && state.peek() == '>') {
        state.position += 2;
        return ParseResult{ParseError::Success, "", std::nullopt};
    }

    if (state.current() != '>') return makeError(ParseError::InvalidTag, "Expected '>'", state.position);
    state.advance();

    state.tagStack.push_back(tagName);

    result = parseContent(state, element);
    if (!result.isSuccess()) return result;

    result = parseClosingTag(state, tagName);
    if (!result.isSuccess()) return result;

    if (!state.tagStack.empty() && state.tagStack.back() == tagName) {
        state.tagStack.pop_back();
    }
    return ParseResult{ParseError::Success, "", std::nullopt};
}

ParseResult XMLParser::parseAttributes(ParseState& state, ElementNode* element) noexcept {
    state.skipWhitespace();
    while (!state.atEnd() && state.current() != '>' && !(state.current() == '/' && state.peek() == '>')) {
        auto optName = state.parseName();
        if (!optName) break;
        state.skipWhitespace();
        if (state.current() != '=') return makeError(ParseError::InvalidAttribute, "Expected '='", state.position);
        state.advance();
        state.skipWhitespace();
        auto optValue = state.parseQuotedString();
        if (!optValue) return makeError(ParseError::InvalidAttribute, "Expected quoted attribute value", state.position);

        std::string_view value;
        if (EntityTable::has_entity(*optValue)) {
            std::string decoded = EntityTable::decode_entities(*optValue);
            value = state.document->getAllocator().intern(decoded);
        } else {
            value = *optValue;
        }
        
        std::string_view attrName = *optName;
        
        if (attrName == "xmlns") {
            element->addNamespaceDeclaration(std::string_view{}, value);
            if (!element->getPrefix().has_value()) {
                element->setNamespaceUri(value);
            }
        } else if (attrName.starts_with("xmlns:")) {
            std::string_view prefix = attrName.substr(6);
            element->addNamespaceDeclaration(prefix, value);
            
            auto elemPrefix = element->getPrefix();
            if (elemPrefix.has_value() && elemPrefix.value() == prefix) {
                element->setNamespaceUri(value);
            }
        }
        
        element->setAttribute(attrName, value);
        state.skipWhitespace();
    }
    return ParseResult{ParseError::Success, "", std::nullopt};
}

ParseResult XMLParser::parseCDATA(ParseState& state, DOMNode* parent) noexcept {
    state.position += 9;
    size_t start = state.position;
    while (!state.atEnd()) {
        if (state.current() == ']' && state.peek() == ']') {
            if (state.peek(2) == '>') {
                std::string_view data = state.input.substr(start, state.position - start);
                state.position += 3;
                auto cdataNode = state.document->getAllocator().create<CDataNode>(data);
                parent->appendChild(cdataNode);
                return ParseResult{ParseError::Success, "", std::nullopt};
            }
        }
        state.advance();
    }
    return makeError(ParseError::InvalidCDATA, "Unclosed CDATA section", state.position);
}

ParseResult XMLParser::parseComment(ParseState& state, DOMNode* parent) noexcept {
    if (!m_config.parseComments) {
        size_t end = state.input.find("-->", state.position + 4);
        if (end == std::string_view::npos) 
            return makeError(ParseError::InvalidComment, "Unclosed comment", state.position);
        state.position = end + 3;
        return ParseResult{ParseError::Success, "", std::nullopt};
    }
    state.position += 4;
    size_t start = state.position;
    while (!state.atEnd()) {
        if (state.current() == '-' && state.peek() == '-') {
            if (state.peek(2) == '>') {
                std::string_view comment = state.input.substr(start, state.position - start);
                state.position += 3;
                auto commentNode = state.document->getAllocator().create<CommentNode>(comment);
                parent->appendChild(commentNode);
                return ParseResult{ParseError::Success, "", std::nullopt};
            }
        }
        state.advance();
    }
    return makeError(ParseError::InvalidComment, "Unclosed comment", state.position);
}

ParseResult XMLParser::parseProcessingInstruction(ParseState& state, DOMNode* /*parent*/) noexcept {
    state.position += 2;
    auto optTarget = state.parseName();
    if (!optTarget) return makeError(ParseError::InvalidProcessingInstruction, "Expected PI target", state.position);
    size_t end = state.input.find("?>", state.position);
    if (end == std::string_view::npos) 
        return makeError(ParseError::InvalidProcessingInstruction, "Unclosed PI", state.position);
    state.position = end + 2;
    return ParseResult{ParseError::Success, "", std::nullopt};
}

ParseResult XMLParser::parseClosingTag(ParseState& state, std::string_view tagName) noexcept {
    if (state.current() != '<') return makeError(ParseError::InvalidTag, "Expected '<'", state.position);
    state.advance();
    if (state.current() != '/') return makeError(ParseError::InvalidTag, "Expected '/'", state.position);
    state.advance();
    auto optCloseName = state.parseName();
    if (!optCloseName) return makeError(ParseError::InvalidTag, "Expected closing tag name", state.position);
    if (*optCloseName != tagName) {
        return makeError(ParseError::MismatchedTag, 
            std::format("Closing tag mismatch: </{}> != </{}>", tagName, *optCloseName), state.position);
    }
    state.skipWhitespace();
    if (state.current() != '>') return makeError(ParseError::InvalidTag, "Expected '>'", state.position);
    state.advance();
    return ParseResult{ParseError::Success, "", std::nullopt};
}

ParseResult XMLParser::parseDocument(ParseState& state) noexcept {
    return parseContent(state, state.document);
}

} // namespace litexml