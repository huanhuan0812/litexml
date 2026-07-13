#include "xml_parser.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <format>

namespace litexml {

XMLParser::XMLParser(const Config& config) : m_config(config) {}
XMLParser::XMLParser() : m_config(Config{}) {}

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
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
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

ParseResult XMLParser::makeError(ParseError error, std::string_view message, std::optional<size_t> pos) const noexcept {
    return ParseResult{.error = error, .message = std::string(message), .position = pos};
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
            return std::unexpected(makeError(ParseError::FileNotFound, std::format("Cannot open file: {}", filename)));
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        auto buffer = std::make_unique<std::string>(size, '\0');
        if (!file.read(buffer->data(), size)) {
            return std::unexpected(makeError(ParseError::IOError, "Failed to read file"));
        }
        
        auto doc = std::make_unique<DocumentNode>();
        doc->setOwnedBuffer(std::move(buffer)); // 让 Document 拥有原始数据
        return parseInternal(*doc->getOwnedBuffer(), std::move(doc));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(ParseError::IOError, e.what()));
    }
}

ParseResultT<std::unique_ptr<DocumentNode>> XMLParser::parseInternal(std::string_view xml, std::unique_ptr<DocumentNode> doc) noexcept {
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
        return ParseResult{ParseError::Success};
    }
    return makeError(ParseError::InvalidProlog, "Expected '?>'", state.position);
}

ParseResult XMLParser::parseContent(ParseState& state, DOMNode* parent) noexcept {
    while (!state.atEnd()) {
        if (state.current() == '<') {
            if (state.peek() == '/') return ParseResult{ParseError::Success};
            else if (state.peek() == '!') {
                if (state.startsWith("<!--")) {
                    auto result = parseComment(state, parent);
                    if (!result.isSuccess()) return result;
                } else if (state.startsWith("<![CDATA[")) {
                    auto result = parseCDATA(state, parent);
                    if (!result.isSuccess()) return result;
                } else {
                    size_t end = state.input.find('>', state.position + 1);
                    if (end == std::string_view::npos) return makeError(ParseError::UnexpectedEOF, "Unclosed DOCTYPE", state.position);
                    state.position = end + 1;
                }
            } else if (state.peek() == '?') {
                if (m_config.parseProcessingInstructions) {
                    auto result = parseProcessingInstruction(state, parent);
                    if (!result.isSuccess()) return result;
                } else {
                    size_t end = state.input.find("?>", state.position + 2);
                    if (end == std::string_view::npos) return makeError(ParseError::UnexpectedEOF, "Unclosed PI", state.position);
                    state.position = end + 2;
                }
            } else {
                auto result = parseElement(state, parent);
                if (!result.isSuccess()) return result;
            }
        } else {
            auto result = parseText(state, parent);
            if (!result.isSuccess()) return result;
        }
    }
    return ParseResult{ParseError::Success};
}

ParseResult XMLParser::parseElement(ParseState& state, DOMNode* parent) noexcept {
    if (state.current() != '<') return makeError(ParseError::InvalidTag, "Expected '<'", state.position);
    state.advance();
    auto optTagName = state.parseName();
    if (!optTagName) return makeError(ParseError::InvalidTag, "Expected element name", state.position);

    std::string_view tagName = *optTagName;
    if (state.tagStack.size() >= m_config.maxDepth) {
        return makeError(ParseError::MalformedXML, std::format("Maximum nesting depth {} exceeded", m_config.maxDepth));
    }

    // 使用内存池分配节点
    auto element = state.document->getAllocator().create<ElementNode>(tagName);
    
    auto result = parseAttributes(state, element);
    if (!result.isSuccess()) return result;

    if (state.current() == '/' && state.peek() == '>') {
        state.position += 2;
        parent->appendChild(element);
        return ParseResult{ParseError::Success};
    }

    if (state.current() != '>') return makeError(ParseError::InvalidTag, "Expected '>'", state.position);
    state.advance();

    parent->appendChild(element);
    state.tagStack.push_back(tagName);

    result = parseContent(state, element);
    if (!result.isSuccess()) return result;

    result = parseClosingTag(state, tagName);
    if (!result.isSuccess()) return result;

    if (!state.tagStack.empty() && state.tagStack.back() == tagName) {
        state.tagStack.pop_back();
    }
    return ParseResult{ParseError::Success};
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

        std::string_view value = internOrOriginal(state, *optValue);
        element->setAttribute(*optName, value);
        state.skipWhitespace();
    }
    return ParseResult{ParseError::Success};
}

ParseResult XMLParser::parseText(ParseState& state, DOMNode* parent) noexcept {
    size_t start = state.position;
    while (!state.atEnd() && state.current() != '<') {
        state.advance();
    }
    if (start == state.position) return ParseResult{ParseError::Success};

    std::string_view text = state.input.substr(start, state.position - start);
    text = internOrOriginal(state, text); // 处理转义并池化

    if (!m_config.preserveWhitespace) {
        if (std::ranges::all_of(text, [](char c) { return std::isspace(static_cast<unsigned char>(c)); })) {
            return ParseResult{ParseError::Success};
        }
    }

    auto textNode = state.document->getAllocator().create<TextNode>(text);
    parent->appendChild(textNode);
    return ParseResult{ParseError::Success};
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
                return ParseResult{ParseError::Success};
            }
        }
        state.advance();
    }
    return makeError(ParseError::InvalidCDATA, "Unclosed CDATA section", state.position);
}

ParseResult XMLParser::parseComment(ParseState& state, DOMNode* parent) noexcept {
    if (!m_config.parseComments) {
        size_t end = state.input.find("-->", state.position + 4);
        if (end == std::string_view::npos) return makeError(ParseError::InvalidComment, "Unclosed comment", state.position);
        state.position = end + 3;
        return ParseResult{ParseError::Success};
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
                return ParseResult{ParseError::Success};
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
    if (end == std::string_view::npos) return makeError(ParseError::InvalidProcessingInstruction, "Unclosed PI", state.position);
    state.position = end + 2;
    return ParseResult{ParseError::Success};
}

ParseResult XMLParser::parseClosingTag(ParseState& state, std::string_view tagName) noexcept {
    if (state.current() != '<') return makeError(ParseError::InvalidTag, "Expected '<'", state.position);
    state.advance();
    if (state.current() != '/') return makeError(ParseError::InvalidTag, "Expected '/'", state.position);
    state.advance();
    auto optCloseName = state.parseName();
    if (!optCloseName) return makeError(ParseError::InvalidTag, "Expected closing tag name", state.position);
    if (*optCloseName != tagName) {
        return makeError(ParseError::MismatchedTag, std::format("Closing tag mismatch: </{}> != </{}>", tagName, *optCloseName), state.position);
    }
    state.skipWhitespace();
    if (state.current() != '>') return makeError(ParseError::InvalidTag, "Expected '>'", state.position);
    state.advance();
    return ParseResult{ParseError::Success};
}

// ==================== 辅助函数 ====================
std::string_view XMLParser::internOrOriginal(ParseState& state, std::string_view text) {
    // 零拷贝优化：如果没有转义字符，直接返回原 buffer 的视图
    if (text.find('&') == std::string_view::npos) {
        return text;
    }
    // 如果有转义字符，解码后放入内存池
    std::string unescaped = unescapeXML(text);
    return state.document->getAllocator().intern(unescaped);
}

std::string XMLParser::unescapeXML(std::string_view text) const noexcept {
    std::string result;
    result.reserve(text.length());
    size_t i = 0;
    while (i < text.length()) {
        if (text[i] == '&') {
            if (text.substr(i).starts_with("&amp;")) { result += '&'; i += 5; }
            else if (text.substr(i).starts_with("&lt;")) { result += '<'; i += 4; }
            else if (text.substr(i).starts_with("&gt;")) { result += '>'; i += 4; }
            else if (text.substr(i).starts_with("&quot;")) { result += '"'; i += 6; }
            else if (text.substr(i).starts_with("&apos;")) { result += '\''; i += 6; }
            else { result += '&'; i++; }
        } else {
            result += text[i];
            i++;
        }
    }
    return result;
}

bool XMLParser::isValidName(std::string_view name) const noexcept {
    if (name.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') return false;
    return std::ranges::all_of(name.substr(1), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
    });
}

} // namespace litexml