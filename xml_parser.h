// xml_parser.h
#ifndef LITEXML_XML_PARSER_H
#define LITEXML_XML_PARSER_H

#include "dom_node.h"
#include "entity_table.h"
#include <string>
#include <memory>
#include <string_view>
#include <expected>
#include <vector>
#include <format>
#include <optional>

namespace litexml {

enum class ParseError : uint8_t {
    Success = 0, UnexpectedEOF, InvalidTag, MismatchedTag, InvalidAttribute,
    InvalidCharacter, InvalidEntity, InvalidProlog, InvalidCDATA, InvalidComment,
    InvalidProcessingInstruction, MalformedXML, FileNotFound, IOError
};

struct ParseResult {
    ParseError error{ParseError::Success};
    std::string message;
    std::optional<size_t> position;

    [[nodiscard]] bool isSuccess() const noexcept { return error == ParseError::Success; }
    [[nodiscard]] std::string toString() const {
        if (isSuccess()) return "Success";
        return std::format("Error: {} at position {}", message, position.value_or(0));
    }
};

template<typename T>
using ParseResultT = std::expected<T, ParseResult>;

class XMLParser {
public:
    struct Config {
        bool preserveWhitespace{false};
        bool parseComments{false};
        bool parseProcessingInstructions{false};
        bool strictMode{true};
        bool parseNamespaces{true};
        size_t maxDepth{1000};
    };

    explicit XMLParser(const Config& config);
    XMLParser();

    [[nodiscard]] ParseResultT<std::unique_ptr<DocumentNode>> parse(std::string_view xml) noexcept;
    [[nodiscard]] ParseResultT<std::unique_ptr<DocumentNode>> parseFile(std::string_view filename) noexcept;

    void setConfig(const Config& config) { m_config = config; }
    [[nodiscard]] const Config& getConfig() const noexcept { return m_config; }

private:
    struct ParseState {
        std::string_view input;
        size_t position{0};
        Config config;
        DocumentNode* document;
        std::vector<std::string_view> tagStack;
        TextAccumulator text_accumulator;
        
        explicit ParseState(std::string_view xml, const Config& cfg, DocumentNode* doc)
            : input(xml), config(cfg), document(doc), 
              text_accumulator(&doc->getAllocator()) {
            tagStack.reserve(32);
            text_accumulator.reserve(2048);
        }
        
        inline TextAccumulator& get_text_accumulator() {
            text_accumulator.clear();
            return text_accumulator;
        }

        [[nodiscard]] inline bool atEnd() const noexcept { 
            return position >= input.length(); 
        }
        
        [[nodiscard]] inline char current() const noexcept { 
            return atEnd() ? '\0' : input[position]; 
        }
        
        [[nodiscard]] inline char peek(size_t offset = 1) const noexcept {
            size_t idx = position + offset;
            return idx < input.length() ? input[idx] : '\0';
        }
        
        inline void advance(size_t n = 1) noexcept { 
            position = std::min(position + n, input.length()); 
        }
        
        void skipWhitespace() noexcept;

        [[nodiscard]] std::optional<std::string_view> parseName() noexcept;
        [[nodiscard]] std::optional<std::string_view> parseQuotedString() noexcept;
        [[nodiscard]] std::optional<std::string_view> parseUntil(char delimiter) noexcept;

        [[nodiscard]] inline bool startsWith(std::string_view str) const noexcept {
            return input.substr(position).starts_with(str);
        }
    };

    Config m_config;

    ParseResultT<std::unique_ptr<DocumentNode>> parseInternal(std::string_view xml, 
                                                               std::unique_ptr<DocumentNode> doc) noexcept;
    
    ParseResult parseDocument(ParseState& state) noexcept;
    ParseResult parseProlog(ParseState& state) noexcept;
    ParseResult parseElement(ParseState& state, DOMNode* parent) noexcept;
    ParseResult parseContent(ParseState& state, DOMNode* parent) noexcept;
    ParseResult parseTextOptimized(ParseState& state, DOMNode* parent) noexcept;
    ParseResult parseCDATA(ParseState& state, DOMNode* parent) noexcept;
    ParseResult parseComment(ParseState& state, DOMNode* parent) noexcept;
    ParseResult parseProcessingInstruction(ParseState& state, DOMNode* parent) noexcept;
    ParseResult parseAttributes(ParseState& state, ElementNode* element) noexcept;
    ParseResult parseClosingTag(ParseState& state, std::string_view tagName) noexcept;

    void resolveElementNamespace(ParseState& state, ElementNode* element) noexcept;
    std::optional<std::string_view> resolveNamespace(ParseState& state, ElementNode* element, 
                                                      std::string_view prefix) noexcept;

    [[nodiscard]] inline bool isWhitespaceOnly(std::string_view text) const noexcept;
    [[nodiscard]] bool isValidName(std::string_view name) const noexcept;
    ParseResult makeError(ParseError error, std::string_view message, 
                          std::optional<size_t> pos = std::nullopt) const noexcept;
    void resolveElementNamespace(ParseState& state, ElementNode* element, DOMNode* parent) noexcept;
    std::optional<std::string_view> resolveNamespace(ParseState& state, ElementNode* element, 
    DOMNode* parent, std::string_view prefix) noexcept;
};

} // namespace litexml
#endif // LITEXML_XML_PARSER_H