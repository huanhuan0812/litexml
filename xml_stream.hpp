// litexml_stream.hpp - 高性能版本
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <variant>
#include <functional>
#include <concepts>
#include <algorithm>
#include <charconv>
#include <system_error>
#include <memory>
#include <stack>
#include <deque>
#include <utility>
#include <format>
#include <filesystem>
#include <fstream>
#include <regex>
#include <chrono>
#include <cctype>
#include "entity_table.h"

namespace litexml::stream {

// ============ 前向声明 ============
class Attribute;
class ElementInfo;
class ParseEvent;
class ParseError;
class EventHandler;

// ============ 位置信息 ============
struct Location {
    size_t line{1};
    size_t column{1};
    size_t offset{0};
    
    std::string to_string() const {
        return std::format("line {}, column {}, offset {}", line, column, offset);
    }
};

// ============ 事件类型 ============
enum class EventType {
    StartDocument,
    EndDocument,
    StartElement,
    EndElement,
    Characters,
    CDATA,
    Comment,
    ProcessingInstruction,
    Whitespace,
    EntityReference,
    Error
};

// ============ 命名空间支持 ============
struct Namespace {
    std::string prefix;
    std::string uri;
    
    bool operator==(const Namespace& other) const {
        return prefix == other.prefix && uri == other.uri;
    }
};

// ============ XML属性 ============
class Attribute {
public:
    Attribute() = default;
    Attribute(std::string name, std::string value, Location loc = {})
        : name_(std::move(name)), value_(std::move(value)), location_(loc) {
        parse_namespace_info();
    }
    
    const std::string& name() const noexcept { return name_; }
    const std::string& value() const noexcept { return value_; }
    const Location& location() const noexcept { return location_; }
    const std::string& namespace_uri() const noexcept { return namespace_uri_; }
    const std::string& local_name() const noexcept { return local_name_; }
    const std::string& prefix() const noexcept { return prefix_; }
    
    template<std::integral T>
    std::optional<T> as_integer(int base = 10) const {
        T value;
        auto [ptr, ec] = std::from_chars(value_.data(), value_.data() + value_.size(), value, base);
        if (ec == std::errc()) {
            return value;
        }
        return std::nullopt;
    }
    
    template<std::floating_point T>
    std::optional<T> as_float() const {
        try {
            if constexpr (std::is_same_v<T, float>) {
                return std::stof(value_);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::stod(value_);
            } else {
                return std::stold(value_);
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    
    bool as_boolean() const {
        std::string lower = value_;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower == "true" || lower == "1" || lower == "yes" || lower == "on";
    }
    
    std::optional<std::chrono::system_clock::time_point> as_datetime() const {
        std::regex iso_regex(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?(?:([+-])(\d{2}):(\d{2})|Z)?)");
        std::smatch match;
        if (std::regex_match(value_, match, iso_regex)) {
            try {
                int year = std::stoi(match[1]);
                int month = std::stoi(match[2]);
                int day = std::stoi(match[3]);
                int hour = std::stoi(match[4]);
                int minute = std::stoi(match[5]);
                int second = std::stoi(match[6]);
                
                std::tm tm = {};
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                tm.tm_hour = hour;
                tm.tm_min = minute;
                tm.tm_sec = second;
                tm.tm_isdst = -1;
                
                auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                return tp;
            } catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

private:
    void parse_namespace_info() {
        auto colon_pos = name_.find(':');
        if (colon_pos != std::string::npos) {
            prefix_ = name_.substr(0, colon_pos);
            local_name_ = name_.substr(colon_pos + 1);
        } else {
            prefix_ = "";
            local_name_ = name_;
        }
    }
    
    std::string name_;
    std::string value_;
    std::string namespace_uri_;
    std::string local_name_;
    std::string prefix_;
    Location location_;
};

// ============ XML节点信息 ============
class ElementInfo {
public:
    ElementInfo() = default;
    ElementInfo(std::string name, Location start, Location end = {})
        : name_(std::move(name)), start_location_(start), end_location_(end) {}
    
    const std::string& name() const noexcept { return name_; }
    const Location& start_location() const noexcept { return start_location_; }
    const Location& end_location() const noexcept { return end_location_; }
    
    void add_attribute(Attribute attr) {
        attributes_.push_back(std::move(attr));
    }
    
    const std::vector<Attribute>& attributes() const noexcept { return attributes_; }
    
    std::optional<Attribute> find_attribute(std::string_view name) const {
        auto it = std::ranges::find_if(attributes_, [name](const Attribute& attr) {
            return attr.name() == name;
        });
        if (it != attributes_.end()) {
            return *it;
        }
        return std::nullopt;
    }
    
    std::optional<Attribute> find_attribute(std::string_view namespace_uri, std::string_view local_name) const {
        auto it = std::ranges::find_if(attributes_, [namespace_uri, local_name](const Attribute& attr) {
            return attr.namespace_uri() == namespace_uri && attr.local_name() == local_name;
        });
        if (it != attributes_.end()) {
            return *it;
        }
        return std::nullopt;
    }
    
    std::optional<std::string_view> attribute_value(std::string_view name) const {
        auto attr = find_attribute(name);
        if (attr) {
            return attr->value();
        }
        return std::nullopt;
    }
    
    void add_namespace(Namespace ns) {
        namespaces_.push_back(std::move(ns));
    }
    
    const std::vector<Namespace>& namespaces() const noexcept { return namespaces_; }
    
    std::optional<std::string_view> get_namespace_uri(std::string_view prefix) const {
        auto it = std::ranges::find_if(namespaces_, [prefix](const Namespace& ns) {
            return ns.prefix == prefix;
        });
        if (it != namespaces_.end()) {
            return it->uri;
        }
        return std::nullopt;
    }
    
    void set_namespace_uri(std::string uri) { namespace_uri_ = std::move(uri); }
    const std::string& namespace_uri() const noexcept { return namespace_uri_; }
    
    void set_local_name(std::string name) { local_name_ = std::move(name); }
    const std::string& local_name() const noexcept { return local_name_; }
    
    void set_depth(size_t depth) noexcept { depth_ = depth; }
    size_t depth() const noexcept { return depth_; }
    
    void set_empty(bool empty) noexcept { is_empty_ = empty; }
    bool is_empty() const noexcept { return is_empty_; }

private:
    std::string name_;
    std::string namespace_uri_;
    std::string local_name_;
    Location start_location_;
    Location end_location_;
    std::vector<Attribute> attributes_;
    std::vector<Namespace> namespaces_;
    size_t depth_{0};
    bool is_empty_{false};
};

// ============ 解析事件 ============
class ParseEvent {
public:
    ParseEvent() = default;
    ParseEvent(EventType type, Location loc = {})
        : type_(type), location_(loc) {}
    
    EventType type() const noexcept { return type_; }
    const Location& location() const noexcept { return location_; }
    
    const std::string& text() const {
        if (auto* s = std::get_if<std::string>(&data_)) {
            return *s;
        }
        static const std::string empty;
        return empty;
    }
    
    const ElementInfo& element() const {
        if (auto* e = std::get_if<ElementInfo>(&data_)) {
            return *e;
        }
        static ElementInfo empty;
        return empty;
    }
    
    void set_text(std::string text) {
        data_ = std::move(text);
    }
    
    void set_element(ElementInfo elem) {
        data_ = std::move(elem);
    }
    
    bool is_start_element() const { return type_ == EventType::StartElement; }
    bool is_end_element() const { return type_ == EventType::EndElement; }
    bool is_characters() const { return type_ == EventType::Characters; }
    bool is_whitespace() const { return type_ == EventType::Whitespace; }

private:
    EventType type_{EventType::Error};
    Location location_;
    std::variant<std::monostate, std::string, ElementInfo> data_;
};

// ============ 错误处理 ============
class ParseError : public std::runtime_error {
public:
    ParseError(std::string message, Location loc = {})
        : std::runtime_error(std::format("XML parse error at {}: {}", loc.to_string(), message)),
          location_(loc) {}
    
    const Location& location() const noexcept { return location_; }

private:
    Location location_;
};

// ============ 回调处理器接口 ============
class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual void on_start_document() {}
    virtual void on_end_document() {}
    virtual void on_start_element(const ElementInfo& /* element */) {}
    virtual void on_end_element(const ElementInfo& /* element */) {}
    virtual void on_characters(std::string_view /* text */) {}
    virtual void on_cdata(std::string_view /* text */) {}
    virtual void on_comment(std::string_view /* text */) {}
    virtual void on_whitespace(std::string_view /* text */) {}
    virtual void on_processing_instruction(std::string_view /* target */, std::string_view /* data */) {}
    virtual void on_error(const ParseError& /* error */) {}
};

// ============ 读取配置 ============
struct ReaderConfig {
    bool preserve_whitespace{false};
    bool coalesce_text{true};
    bool expand_entities{true};
    bool validate_names{true};
    bool namespace_aware{true};
    bool strict_parsing{true};
    bool support_extended_entities{true};
    bool support_numeric_entities{true};
    bool strict_mode{true};
    bool require_closed_tags{true};
    bool allow_incomplete_documents{false};
    size_t max_depth{1024};
    size_t max_attribute_count{1024};
    size_t max_text_length{1024 * 1024};
    std::string encoding{"UTF-8"};
    bool detect_encoding{true};
    bool normalize_unicode{true};
    
    // 性能优化配置
    size_t buffer_size{8192};
    size_t chunk_size{4096};
    bool prefetch_data{true};
};

// ============ Unicode编码类型 ============
enum class Encoding {
    UTF_8,
    UTF_16_LE,
    UTF_16_BE,
    UTF_32_LE,
    UTF_32_BE,
    Unknown
};

// ============ Unicode辅助类 ============
class UnicodeConverter {
public:
    static Encoding detect_bom(std::string_view data) {
        if (data.size() < 2) return Encoding::UTF_8;
        
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data());
        
        if (data.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            return Encoding::UTF_8;
        }
        if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
            return Encoding::UTF_16_LE;
        }
        if (bytes[0] == 0xFE && bytes[1] == 0xFF) {
            return Encoding::UTF_16_BE;
        }
        if (data.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xFE && 
            bytes[2] == 0x00 && bytes[3] == 0x00) {
            return Encoding::UTF_32_LE;
        }
        if (data.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0x00 && 
            bytes[2] == 0xFE && bytes[3] == 0xFF) {
            return Encoding::UTF_32_BE;
        }
        
        return Encoding::UTF_8;
    }
    
    static Encoding detect_from_xml_declaration(std::string_view data) {
        const std::string_view encoding_pattern = "encoding";
        auto pos = data.find(encoding_pattern);
        if (pos == std::string_view::npos) return Encoding::UTF_8;
        
        pos = data.find('=', pos);
        if (pos == std::string_view::npos) return Encoding::UTF_8;
        
        pos++;
        while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos]))) {
            pos++;
        }
        
        if (pos >= data.size()) return Encoding::UTF_8;
        
        char quote = data[pos];
        if (quote != '"' && quote != '\'') return Encoding::UTF_8;
        
        pos++;
        size_t end_pos = data.find(quote, pos);
        if (end_pos == std::string_view::npos) return Encoding::UTF_8;
        
        std::string encoding_name(data.substr(pos, end_pos - pos));
        std::transform(encoding_name.begin(), encoding_name.end(), encoding_name.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        
        if (encoding_name == "utf-8" || encoding_name == "utf8") {
            return Encoding::UTF_8;
        } else if (encoding_name == "utf-16le" || encoding_name == "utf_16le" || encoding_name == "utf16le") {
            return Encoding::UTF_16_LE;
        } else if (encoding_name == "utf-16be" || encoding_name == "utf_16be" || encoding_name == "utf16be") {
            return Encoding::UTF_16_BE;
        } else if (encoding_name == "utf-16") {
            return Encoding::UTF_16_BE;
        } else if (encoding_name == "utf-32le" || encoding_name == "utf_32le" || encoding_name == "utf32le") {
            return Encoding::UTF_32_LE;
        } else if (encoding_name == "utf-32be" || encoding_name == "utf_32be" || encoding_name == "utf32be") {
            return Encoding::UTF_32_BE;
        } else if (encoding_name == "utf-32") {
            return Encoding::UTF_32_BE;
        }
        
        return Encoding::UTF_8;
    }
};

// ============ Unicode字符串解码器 ============
class UnicodeDecoder {
public:
    explicit UnicodeDecoder(Encoding encoding = Encoding::UTF_8)
        : encoding_(encoding) {}
    
    std::string decode(std::string_view data) const {
        switch (encoding_) {
            case Encoding::UTF_8:
                return std::string(data);
            case Encoding::UTF_16_LE:
                return decode_utf16(data, false);
            case Encoding::UTF_16_BE:
                return decode_utf16(data, true);
            case Encoding::UTF_32_LE:
                return decode_utf32(data, false);
            case Encoding::UTF_32_BE:
                return decode_utf32(data, true);
            default:
                return std::string(data);
        }
    }
    
    static char32_t read_utf8_char(std::string_view data, size_t& pos) {
        if (pos >= data.size()) return U'\0';
        
        unsigned char c = static_cast<unsigned char>(data[pos]);
        size_t bytes = 0;
        char32_t codepoint = 0;
        
        if ((c & 0x80) == 0) {
            bytes = 1;
            codepoint = c;
        } else if ((c & 0xE0) == 0xC0) {
            bytes = 2;
            codepoint = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            bytes = 3;
            codepoint = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            bytes = 4;
            codepoint = c & 0x07;
        } else {
            pos++;
            return U'\xFFFD';
        }
        
        if (pos + bytes > data.size()) {
            pos = data.size();
            return U'\xFFFD';
        }
        
        for (size_t i = 1; i < bytes; i++) {
            unsigned char next = static_cast<unsigned char>(data[pos + i]);
            if ((next & 0xC0) != 0x80) {
                pos += i + 1;
                return U'\xFFFD';
            }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        
        pos += bytes;
        return codepoint;
    }
    
    static std::string encode_utf8(char32_t codepoint) {
        std::string result;
        if (codepoint < 0x80) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint < 0x110000) {
            result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            result = "\xEF\xBF\xBD";
        }
        return result;
    }
    
    static bool is_valid_codepoint(char32_t codepoint) {
        return (codepoint < 0xD800 || codepoint > 0xDFFF) && codepoint <= 0x10FFFF;
    }

private:
    Encoding encoding_;
    
    static std::string decode_utf16(std::string_view data, bool big_endian) {
        std::string result;
        if (data.size() < 2) return result;
        
        size_t pos = 0;
        if (data.size() >= 2) {
            uint16_t bom = read_uint16(data, pos, big_endian);
            if (bom == 0xFEFF) {
                pos += 2;
            } else {
                pos = 0;
            }
        }
        
        while (pos + 2 <= data.size()) {
            uint16_t word = read_uint16(data, pos, big_endian);
            pos += 2;
            
            char32_t codepoint;
            if (word >= 0xD800 && word <= 0xDBFF) {
                if (pos + 2 > data.size()) {
                    result += "\xEF\xBF\xBD";
                    break;
                }
                uint16_t low = read_uint16(data, pos, big_endian);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    pos += 2;
                    codepoint = ((word - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                } else {
                    result += "\xEF\xBF\xBD";
                    continue;
                }
            } else if (word >= 0xDC00 && word <= 0xDFFF) {
                result += "\xEF\xBF\xBD";
                continue;
            } else {
                codepoint = word;
            }
            
            if (is_valid_codepoint(codepoint)) {
                result += encode_utf8(codepoint);
            } else {
                result += "\xEF\xBF\xBD";
            }
        }
        
        return result;
    }
    
    static std::string decode_utf32(std::string_view data, bool big_endian) {
        std::string result;
        if (data.size() < 4) return result;
        
        size_t pos = 0;
        if (data.size() >= 4) {
            uint32_t bom = read_uint32(data, pos, big_endian);
            if (bom == 0xFEFF) {
                pos += 4;
            } else {
                pos = 0;
            }
        }
        
        while (pos + 4 <= data.size()) {
            char32_t codepoint = read_uint32(data, pos, big_endian);
            pos += 4;
            
            if (is_valid_codepoint(codepoint)) {
                result += encode_utf8(codepoint);
            } else {
                result += "\xEF\xBF\xBD";
            }
        }
        
        return result;
    }
    
    static uint16_t read_uint16(std::string_view data, size_t pos, bool big_endian) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data() + pos);
        if (big_endian) {
            return (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
        } else {
            return bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8);
        }
    }
    
    static uint32_t read_uint32(std::string_view data, size_t pos, bool big_endian) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data() + pos);
        if (big_endian) {
            return (static_cast<uint32_t>(bytes[0]) << 24) |
                   (static_cast<uint32_t>(bytes[1]) << 16) |
                   (static_cast<uint32_t>(bytes[2]) << 8) |
                   bytes[3];
        } else {
            return bytes[0] |
                   (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
        }
    }
};

// ============ 高性能缓冲区 ============
class FastBuffer {
public:
    FastBuffer(size_t size = 8192)
        : buffer_(size), read_pos_(0), write_pos_(0) {}
    
    void fill(std::string_view input, size_t& pos) {
        size_t remaining = input.size() - pos;
        if (remaining == 0) {
            write_pos_ = 0;
            read_pos_ = 0;
            return;
        }
        
        size_t to_copy = std::min(remaining, buffer_.size());
        std::memcpy(buffer_.data(), input.data() + pos, to_copy);
        read_pos_ = 0;
        write_pos_ = to_copy;
        pos += to_copy;
    }
    
    void prefetch(std::string_view input, size_t& pos) {
        if (available() < 64 && pos < input.size()) {
            size_t unread = available();
            if (unread > 0 && read_pos_ > 0) {
                std::memmove(buffer_.data(), buffer_.data() + read_pos_, unread);
                read_pos_ = 0;
                write_pos_ = unread;
            } else if (unread == 0) {
                read_pos_ = 0;
                write_pos_ = 0;
            }
            
            size_t remaining = input.size() - pos;
            size_t to_copy = std::min(remaining, buffer_.size() - write_pos_);
            if (to_copy > 0) {
                std::memcpy(buffer_.data() + write_pos_, input.data() + pos, to_copy);
                write_pos_ += to_copy;
                pos += to_copy;
            }
        }
    }
    
    char peek(size_t offset = 0) const {
        size_t idx = read_pos_ + offset;
        return (idx < write_pos_) ? buffer_[idx] : '\0';
    }
    
    char advance() {
        return (read_pos_ < write_pos_) ? buffer_[read_pos_++] : '\0';
    }
    
    void advance(size_t n) {
        read_pos_ = std::min(read_pos_ + n, write_pos_);
    }
    
    bool eof() const { return read_pos_ >= write_pos_; }
    size_t available() const { return write_pos_ - read_pos_; }
    size_t position() const { return read_pos_; }
    
    void reset() { 
        read_pos_ = 0; 
        write_pos_ = 0; 
    }
    
    const char* data() const { return buffer_.data() + read_pos_; }
    size_t size() const { return write_pos_ - read_pos_; }

private:
    std::vector<char> buffer_;
    size_t read_pos_;
    size_t write_pos_;
};

// ============ 流式读取器 ============
class StreamReader {
public:
    using Callback = std::function<void(const ParseEvent&)>;
    using Filter = std::function<bool(const ParseEvent&)>;
    
    StreamReader() : buffer_(8192) {}
    
    StreamReader(std::string_view input, const ReaderConfig& config = {})
        : config_(config), buffer_(config.buffer_size) {
        initialize_with_utf8(input);
    }
    
    StreamReader(std::string_view input, const std::string& encoding, const ReaderConfig& config = {})
        : config_(config), buffer_(config.buffer_size) {
        config_.encoding = encoding;
        initialize_with_encoding(input);
    }
    
    static std::unique_ptr<StreamReader> from_file(const std::filesystem::path& path, 
                                                    const ReaderConfig& config = {}) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw ParseError("Cannot open file: " + path.string());
        }
        
        std::string content((std::istreambuf_iterator<char>(file)), 
                           std::istreambuf_iterator<char>());
        
        auto reader = std::make_unique<StreamReader>();
        reader->config_ = config;
        reader->buffer_ = FastBuffer(config.buffer_size);
        reader->initialize_with_encoding(content);
        return reader;
    }
    
    void set_config(const ReaderConfig& config) noexcept {
        config_ = config;
        buffer_ = FastBuffer(config.buffer_size);
    }
    
    const ReaderConfig& config() const noexcept { return config_; }
    
    void set_preserve_whitespace(bool preserve) noexcept {
        config_.preserve_whitespace = preserve;
    }
    
    void set_callback(Callback callback) {
        callback_ = std::move(callback);
    }
    
    void set_handler(EventHandler* handler) {
        handler_ = handler;
    }
    
    void set_filter(Filter filter) {
        filter_ = std::move(filter);
    }
    
    Encoding detected_encoding() const { return detected_encoding_; }
    size_t depth() const noexcept { return depth_; }
    
    bool parse() {
        try {
            push_event(EventType::StartDocument);
            
            if (config_.prefetch_data) {
                buffer_.prefetch(input_, pos_);
            }
            
            while (!eof()) {
                skip_whitespace();
                if (eof()) break;
                
                char c = peek();
                if (c == '<') {
                    char next = lookahead(1);
                    if (next == '?' && (lookahead(2) == 'x' || lookahead(2) == 'X')) {
                        parse_xml_declaration();
                    } else if (next == '?' && lookahead(2) != '-') {
                        parse_processing_instruction();
                    } else if (next == '!' && lookahead(2) == '-' && lookahead(3) == '-') {
                        parse_comment();
                    } else if (next == '!' && lookahead(2) == '[' && 
                               lookahead(3) == 'C' && lookahead(4) == 'D' &&
                               lookahead(5) == 'A' && lookahead(6) == 'T' &&
                               lookahead(7) == 'A' && lookahead(8) == '[') {
                        flush_text_buffer();
                        parse_cdata();
                    } else if (next == '!' && lookahead(2) == 'D' && lookahead(3) == 'O' && lookahead(4) == 'C') {
                        flush_text_buffer();
                        skip_dtd();
                    } else if (next == '/') {
                        flush_text_buffer();
                        parse_end_element();
                    } else {
                        flush_text_buffer();
                        parse_start_element();
                    }
                } else {
                    collect_text();
                }
                
                if (config_.prefetch_data) {
                    buffer_.prefetch(input_, pos_);
                }
            }
            
            flush_text_buffer();
            
            if (config_.require_closed_tags && !element_stack_.empty()) {
                throw ParseError("Unclosed element: " + element_stack_.top().name(), 
                               current_location_);
            }
            
            push_event(EventType::EndDocument);
            return true;
        } catch (const ParseError& e) {
            if (handler_) {
                handler_->on_error(e);
            }
            if (callback_) {
                ParseEvent event(EventType::Error, e.location());
                callback_(event);
            }
            return false;
        }
    }
    
    std::optional<ParseEvent> next_event() {
        try {
            while (event_queue_.empty()) {
                if (eof()) {
                    if (config_.require_closed_tags && !element_stack_.empty()) {
                        throw ParseError("Unclosed element: " + element_stack_.top().name(), 
                                       current_location_);
                    }
                    return std::nullopt;
                }
                parse_next_event();
                if (event_queue_.empty() && eof()) {
                    return std::nullopt;
                }
            }
            
            auto event = std::move(event_queue_.front());
            event_queue_.pop_front();
            return event;
        } catch (const ParseError& e) {
            ParseEvent event(EventType::Error, e.location());
            return event;
        }
    }

private:
    class TextBuffer {
    public:
        TextBuffer() = default;
        explicit TextBuffer(size_t initial_size) {
            data_.reserve(initial_size);
        }
        
        void append(char c) {
            data_.push_back(c);
        }
        
        void append(std::string_view str) {
            data_.append(str);
        }
        
        void append_utf8(char32_t codepoint) {
            if (codepoint < 0x80) {
                data_.push_back(static_cast<char>(codepoint));
            } else if (codepoint < 0x800) {
                data_.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                data_.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint < 0x10000) {
                data_.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                data_.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                data_.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint < 0x110000) {
                data_.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                data_.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                data_.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                data_.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }
        
        std::string_view view() const { return data_; }
        void clear() { data_.clear(); }
        bool empty() const { return data_.empty(); }
        void reserve(size_t size) { data_.reserve(size); }
        size_t size() const { return data_.size(); }
    
    private:
        std::string data_;
    };
    
    std::string utf8_data_;
    std::string_view input_;
    size_t pos_{0};
    Location current_location_{1, 1, 0};
    ReaderConfig config_;
    Encoding detected_encoding_{Encoding::UTF_8};
    FastBuffer buffer_;
    Callback callback_;
    Filter filter_;
    EventHandler* handler_{nullptr};
    std::deque<ParseEvent> event_queue_;
    std::stack<ElementInfo> element_stack_;
    std::vector<Namespace> namespace_stack_;
    size_t depth_{0};
    TextBuffer text_buffer_;
    
    void initialize_with_utf8(std::string_view input) {
        utf8_data_ = std::string(input);
        input_ = utf8_data_;
        detected_encoding_ = Encoding::UTF_8;
        pos_ = 0;
        current_location_ = {1, 1, 0};
        text_buffer_.reserve(config_.max_text_length / 4);
        buffer_.fill(input_, pos_);
    }
    
    void initialize_with_encoding(std::string_view input) {
        Encoding bom_encoding = UnicodeConverter::detect_bom(input);
        size_t bom_offset = 0;
        
        if (bom_encoding != Encoding::UTF_8) {
            if (bom_encoding == Encoding::UTF_16_LE || bom_encoding == Encoding::UTF_16_BE) {
                bom_offset = 2;
            } else if (bom_encoding == Encoding::UTF_32_LE || bom_encoding == Encoding::UTF_32_BE) {
                bom_offset = 4;
            }
            detected_encoding_ = bom_encoding;
        } else if (config_.detect_encoding) {
            if (input.size() > 50) {
                Encoding xml_encoding = UnicodeConverter::detect_from_xml_declaration(input);
                if (xml_encoding != Encoding::UTF_8) {
                    detected_encoding_ = xml_encoding;
                } else {
                    detected_encoding_ = Encoding::UTF_8;
                }
            } else {
                detected_encoding_ = Encoding::UTF_8;
            }
        } else {
            detected_encoding_ = parse_encoding(config_.encoding);
        }
        
        UnicodeDecoder decoder(detected_encoding_);
        std::string_view data_without_bom = input.substr(bom_offset);
        utf8_data_ = decoder.decode(data_without_bom);
        input_ = utf8_data_;
        pos_ = 0;
        current_location_ = {1, 1, 0};
        text_buffer_.reserve(config_.max_text_length / 4);
        buffer_.fill(input_, pos_);
    }
    
    Encoding parse_encoding(const std::string& encoding_name) {
        std::string name = encoding_name;
        std::transform(name.begin(), name.end(), name.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        
        if (name == "utf-8" || name == "utf8" || name == "utf_8") {
            return Encoding::UTF_8;
        } else if (name == "utf-16le" || name == "utf_16le" || name == "utf16le") {
            return Encoding::UTF_16_LE;
        } else if (name == "utf-16be" || name == "utf_16be" || name == "utf16be") {
            return Encoding::UTF_16_BE;
        } else if (name == "utf-16" || name == "utf_16" || name == "utf16") {
            return Encoding::UTF_16_BE;
        } else if (name == "utf-32le" || name == "utf_32le" || name == "utf32le") {
            return Encoding::UTF_32_LE;
        } else if (name == "utf-32be" || name == "utf_32be" || name == "utf32be") {
            return Encoding::UTF_32_BE;
        } else if (name == "utf-32" || name == "utf_32" || name == "utf32") {
            return Encoding::UTF_32_BE;
        } else {
            return Encoding::UTF_8;
        }
    }
    
    bool eof() const noexcept { 
        return buffer_.eof() && pos_ >= input_.size();
    }
    
    char peek() const { 
        return buffer_.peek();
    }
    
    char lookahead(size_t n) const {
        return buffer_.peek(n);
    }
    
    char32_t peek_unicode() const {
        if (eof()) return U'\0';
        
        const char* data = buffer_.data();
        size_t available = buffer_.size();
        
        if (available < 4) {
            size_t global_pos = pos_ - available;
            if (global_pos >= input_.size()) return U'\0';
            size_t temp_global = global_pos;
            return UnicodeDecoder::read_utf8_char(input_, temp_global);
        }
        
        std::string_view temp_view(data, available);
        size_t local_pos = 0;
        return UnicodeDecoder::read_utf8_char(temp_view, local_pos);
    }
    
    void advance() {
        buffer_.advance();
        if (buffer_.available() < 4 && pos_ < input_.size()) {
            buffer_.prefetch(input_, pos_);
        }
    }
    
    void advance_utf8() {
        if (eof()) return;
        
        unsigned char c = static_cast<unsigned char>(peek());
        size_t bytes = 1;
        
        if ((c & 0x80) == 0) {
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            bytes = 4;
        }
        
        for (size_t i = 0; i < bytes && !eof(); i++) {
            if (peek() == '\n') {
                current_location_.line++;
                current_location_.column = 1;
            } else {
                current_location_.column++;
            }
            advance();
            current_location_.offset++;
        }
    }
    
    void advance(size_t n) {
        for (size_t i = 0; i < n; ++i) {
            advance();
        }
    }
    
    void skip_whitespace() {
        while (!eof()) {
            char32_t c = peek_unicode();
            if (!std::isspace(static_cast<unsigned char>(c)) && c != 0x00A0 && c != 0x2028 && c != 0x2029) {
                break;
            }
            advance_utf8();
        }
    }
    
    bool match(std::string_view pattern) {
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (lookahead(i) != pattern[i]) {
                return false;
            }
        }
        advance(pattern.size());
        return true;
    }
    
    bool is_valid_name_char(char32_t c, bool first) const {
        if (first) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                   c == '_' || c == ':' || (c >= 0x00C0 && c <= 0x00D6) ||
                   (c >= 0x00D8 && c <= 0x00F6) || (c >= 0x00F8 && c <= 0x02FF) ||
                   (c >= 0x0370 && c <= 0x037D) || (c >= 0x037F && c <= 0x1FFF) ||
                   (c >= 0x200C && c <= 0x200D) || (c >= 0x2070 && c <= 0x218F) ||
                   (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF) ||
                   (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) ||
                   (c >= 0x10000 && c <= 0xEFFFF);
        }
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':' ||
               (c >= 0x00B7 && c <= 0x00B7) || (c >= 0x0300 && c <= 0x036F) ||
               (c >= 0x203F && c <= 0x2040) || (c >= 0x00C0 && c <= 0x00D6) ||
               (c >= 0x00D8 && c <= 0x00F6) || (c >= 0x00F8 && c <= 0x02FF) ||
               (c >= 0x0370 && c <= 0x037D) || (c >= 0x037F && c <= 0x1FFF) ||
               (c >= 0x200C && c <= 0x200D) || (c >= 0x2070 && c <= 0x218F) ||
               (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF) ||
               (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) ||
               (c >= 0x10000 && c <= 0xEFFFF);
    }
    
    bool is_valid_xml_name(const std::string& name) const {
        if (name.empty()) return false;
        
        std::u32string utf32 = utf8_to_utf32(name);
        if (utf32.empty()) return false;
        
        if (!is_valid_name_char(utf32[0], true)) return false;
        
        for (size_t i = 1; i < utf32.size(); ++i) {
            if (!is_valid_name_char(utf32[i], false)) return false;
        }
        
        return true;
    }
    
    static std::u32string utf8_to_utf32(const std::string& utf8) {
        std::u32string result;
        size_t pos = 0;
        while (pos < utf8.size()) {
            char32_t c = UnicodeDecoder::read_utf8_char(utf8, pos);
            if (c != U'\0') {
                result.push_back(c);
            }
        }
        return result;
    }
    
    std::string resolve_entity() {
        Location start = current_location_;
        std::string entity;
        entity.reserve(16);
        entity.push_back(peek());
        advance();
        
        if (peek() == '#') {
            if (!config_.support_numeric_entities) {
                throw ParseError("Numeric entities are disabled", start);
            }
            
            entity.push_back(peek());
            advance();
            
            bool is_hex = false;
            if (peek() == 'x' || peek() == 'X') {
                is_hex = true;
                entity.push_back(peek());
                advance();
            }
            
            while (!eof() && peek() != ';') {
                char c = peek();
                if (is_hex) {
                    if (!std::isxdigit(static_cast<unsigned char>(c))) {
                        throw ParseError("Invalid hexadecimal digit in entity", current_location_);
                    }
                } else {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        throw ParseError("Invalid digit in numeric entity", current_location_);
                    }
                }
                entity.push_back(c);
                advance();
            }
            
            if (eof()) {
                throw ParseError("Unterminated numeric entity", start);
            }
            
            entity.push_back(peek());
            advance();
            
            char32_t codepoint = 0;
            if (!EntityTable::parseNumericEntity(entity, codepoint)) {
                if (config_.strict_parsing) {
                    throw ParseError("Invalid numeric entity: " + entity, start);
                }
                return entity;
            }
            
            if (!UnicodeDecoder::is_valid_codepoint(codepoint)) {
                if (config_.strict_parsing) {
                    throw ParseError("Invalid codepoint in numeric entity", start);
                }
                return entity;
            }
            
            return UnicodeDecoder::encode_utf8(codepoint);
        }
        
        if (!config_.support_extended_entities) {
            while (!eof() && peek() != ';') {
                char c = peek();
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
                    throw ParseError("Invalid character in entity name", current_location_);
                }
                entity.push_back(c);
                advance();
            }
        } else {
            while (!eof() && peek() != ';') {
                char c = peek();
                if (!std::isalnum(static_cast<unsigned char>(c)) && 
                    c != '_' && c != '-' && c != '.') {
                    throw ParseError("Invalid character in entity name", current_location_);
                }
                entity.push_back(c);
                advance();
            }
        }
        
        if (eof()) {
            throw ParseError("Unterminated entity", start);
        }
        
        entity.push_back(peek());
        advance();
        
        auto named_result = EntityTable::lookup(entity);
        if (named_result.has_value()) {
            return std::string(1, named_result.value());
        }
        
        if (config_.support_extended_entities) {
            auto extended_result = EntityTable::lookupExtended(entity);
            if (extended_result.has_value()) {
                return std::string(extended_result.value());
            }
        }
        
        if (config_.strict_parsing) {
            throw ParseError("Unknown entity: " + entity, start);
        }
        
        return entity;
    }
    
    void collect_text() {
        while (!eof() && peek() != '<') {
            if (peek() == '&' && config_.expand_entities) {
                text_buffer_.append(resolve_entity());
            } else {
                size_t available = buffer_.size();
                if (available > 0) {
                    const char* data = buffer_.data();
                    size_t i = 0;
                    while (i < available && data[i] != '<' && data[i] != '&') {
                        i++;
                    }
                    if (i > 0) {
                        text_buffer_.append(std::string_view(data, i));
                        advance(i);
                        continue;
                    }
                }
                
                text_buffer_.append(peek());
                advance();
            }
            
            if (text_buffer_.size() > config_.max_text_length) {
                throw ParseError("Text length exceeds maximum", current_location_);
            }
        }
        
        if (!config_.coalesce_text) {
            flush_text_buffer();
        }
    }
    
    void read_name(std::string& name) {
        if (eof()) {
            throw ParseError("Unexpected end of input while reading name", current_location_);
        }
        
        char32_t first = peek_unicode();
        if (!is_valid_name_char(first, true)) {
            throw ParseError("Invalid name start character", current_location_);
        }
        
        name.reserve(32);
        
        while (!eof()) {
            char32_t c = peek_unicode();
            if (!is_valid_name_char(c, false)) {
                break;
            }
            
            unsigned char ch = static_cast<unsigned char>(peek());
            size_t bytes = 1;
            if ((ch & 0x80) == 0) {
                bytes = 1;
            } else if ((ch & 0xE0) == 0xC0) {
                bytes = 2;
            } else if ((ch & 0xF0) == 0xE0) {
                bytes = 3;
            } else if ((ch & 0xF8) == 0xF0) {
                bytes = 4;
            }
            
            for (size_t i = 0; i < bytes && !eof(); i++) {
                name.push_back(peek());
                advance();
            }
        }
    }
    
    void parse_xml_declaration() {
        advance(2);
        skip_whitespace();
        
        if (match("xml") || match("XML")) {
            while (!eof() && peek() != '?') {
                skip_whitespace();
                if (peek() == '?') break;
                
                std::string attr_name;
                read_name(attr_name);
                skip_whitespace();
                
                if (peek() != '=') {
                    throw ParseError("Expected '=' after attribute name", current_location_);
                }
                advance();
                skip_whitespace();
                
                char quote = peek();
                if (quote != '"' && quote != '\'') {
                    throw ParseError("Expected quote character for attribute value", current_location_);
                }
                advance();
                
                std::string attr_value;
                while (!eof() && peek() != quote) {
                    if (peek() == '&' && config_.expand_entities) {
                        attr_value += resolve_entity();
                    } else {
                        attr_value.push_back(peek());
                        advance();
                    }
                }
                
                if (eof()) {
                    throw ParseError("Unclosed attribute value", current_location_);
                }
                advance();
            }
            
            if (peek() == '?') {
                advance();
                if (peek() == '>') {
                    advance();
                } else {
                    throw ParseError("Expected '>' after '?'", current_location_);
                }
            } else {
                throw ParseError("Expected '?'", current_location_);
            }
        } else {
            throw ParseError("Expected XML declaration", current_location_);
        }
    }
    
    void parse_start_element() {
        Location start_loc = current_location_;
        advance();
        
        std::string qname;
        read_name(qname);
        
        if (config_.validate_names && !is_valid_xml_name(qname)) {
            throw ParseError("Invalid element name: " + qname, start_loc);
        }
        
        auto [prefix, local_name] = parse_qname(qname);
        ElementInfo elem(qname, start_loc);
        elem.set_depth(depth_);
        elem.set_local_name(local_name);
        
        size_t attr_count = 0;
        while (!eof() && peek() != '>' && peek() != '/') {
            skip_whitespace();
            if (eof()) break;
            if (peek() == '>' || peek() == '/') break;
            
            if (attr_count++ > config_.max_attribute_count) {
                throw ParseError("Too many attributes", current_location_);
            }
            
            std::string attr_name;
            read_name(attr_name);
            
            if (config_.validate_names && !is_valid_xml_name(attr_name)) {
                throw ParseError("Invalid attribute name: " + attr_name, current_location_);
            }
            
            skip_whitespace();
            
            if (peek() != '=') {
                throw ParseError("Expected '=' after attribute name", current_location_);
            }
            advance();
            skip_whitespace();
            
            char quote = peek();
            if (quote != '"' && quote != '\'') {
                throw ParseError("Expected quote character for attribute value", current_location_);
            }
            advance();
            
            std::string attr_value;
            Location val_start = current_location_;
            while (!eof() && peek() != quote) {
                if (peek() == '&' && config_.expand_entities) {
                    attr_value += resolve_entity();
                } else {
                    attr_value.push_back(peek());
                    advance();
                }
            }
            
            if (eof()) {
                throw ParseError("Unclosed attribute value", val_start);
            }
            advance();
            
            Attribute attr(attr_name, attr_value, 
                          Location{val_start.line, val_start.column, val_start.offset});
            elem.add_attribute(attr);
            
            if (config_.namespace_aware) {
                process_namespace_declaration(attr);
            }
        }
        
        bool self_closing = false;
        if (peek() == '/') {
            advance();
            self_closing = true;
        }
        
        if (peek() != '>') {
            throw ParseError("Expected '>' to close element", current_location_);
        }
        advance();
        
        if (config_.namespace_aware) {
            std::string ns_uri = resolve_namespace(prefix);
            elem.set_namespace_uri(ns_uri);
        }
        
        elem.set_empty(self_closing);
        element_stack_.push(elem);
        depth_++;
        
        push_event(EventType::StartElement, std::move(elem));
        
        if (self_closing) {
            pop_element_stack();
            ElementInfo end_elem(qname, current_location_, current_location_);
            end_elem.set_depth(depth_);
            push_event(EventType::EndElement, std::move(end_elem));
        }
    }
    
    void parse_end_element() {
        Location start_loc = current_location_;
        advance();
        advance();
        
        std::string qname;
        read_name(qname);
        
        if (config_.validate_names && !is_valid_xml_name(qname)) {
            throw ParseError("Invalid element name: " + qname, start_loc);
        }
        
        skip_whitespace();
        
        if (peek() != '>') {
            throw ParseError("Expected '>' to close end tag", current_location_);
        }
        advance();
        
        if (!element_stack_.empty() && element_stack_.top().name() != qname) {
            throw ParseError("Mismatched end tag: expected '" + 
                           element_stack_.top().name() + "', got '" + qname + "'", 
                           start_loc);
        }
        
        pop_element_stack();
        
        ElementInfo elem(qname, start_loc, current_location_);
        elem.set_depth(depth_);
        push_event(EventType::EndElement, std::move(elem));
    }
    
    void parse_cdata() {
        Location start_loc = current_location_;
        advance(9);
        
        std::string text;
        text.reserve(1024);
        
        while (!eof() && !match("]]>")) {
            text.push_back(peek());
            advance();
        }
        
        if (eof()) {
            throw ParseError("Unclosed CDATA section", start_loc);
        }
        
        push_event(EventType::CDATA, std::move(text));
    }
    
    void parse_comment() {
        advance(4);
        
        std::string text;
        text.reserve(256);
        
        while (!eof() && !match("-->")) {
            text.push_back(peek());
            advance();
        }
        
        if (eof()) {
            throw ParseError("Unclosed comment", current_location_);
        }
        
        push_event(EventType::Comment, std::move(text));
    }
    
    void parse_processing_instruction() {
        Location start_loc = current_location_;
        advance(2);
        skip_whitespace();
        
        std::string target;
        read_name(target);
        
        if (target.empty()) {
            throw ParseError("Empty processing instruction target", start_loc);
        }
        
        if (target == "xml" || target == "XML") {
            if (config_.strict_parsing) {
                throw ParseError("Unexpected processing instruction with target 'xml'", start_loc);
            }
            
            while (!eof() && peek() != '?') {
                advance();
            }
            
            if (peek() == '?') {
                advance();
                if (peek() == '>') {
                    advance();
                }
            }
            return;
        }
        
        skip_whitespace();
        
        std::string data;
        data.reserve(256);
        
        while (!eof() && peek() != '?') {
            if (peek() == '&' && config_.expand_entities) {
                data += resolve_entity();
            } else {
                data.push_back(peek());
                advance();
            }
        }
        
        if (peek() == '?') {
            advance();
            if (peek() == '>') {
                advance();
            } else {
                throw ParseError("Expected '>' after '?'", current_location_);
            }
        } else {
            throw ParseError("Expected '?'", current_location_);
        }
        
        push_event(EventType::ProcessingInstruction, target);
        ParseEvent& event = event_queue_.back();
        event.set_text(std::move(data));
    }
    
    void skip_dtd() {
        advance(4);
        
        int bracket_count = 0;
        while (!eof()) {
            char c = peek();
            if (c == '[') {
                bracket_count++;
                advance();
            } else if (c == ']') {
                bracket_count--;
                advance();
            } else if (c == '>') {
                if (bracket_count == 0) {
                    advance();
                    break;
                }
                advance();
            } else {
                advance();
            }
        }
    }
    
    void parse_next_event() {
        skip_whitespace();
        if (eof()) return;
        
        if (depth_ > config_.max_depth) {
            throw ParseError("Maximum nesting depth exceeded", current_location_);
        }
        
        if (peek() == '<') {
            if (lookahead(1) == '?' && (lookahead(2) == 'x' || lookahead(2) == 'X')) {
                parse_xml_declaration();
            } else if (lookahead(1) == '?' && lookahead(2) != '-') {
                parse_processing_instruction();
            } else if (lookahead(1) == '!' && lookahead(2) == '-' && lookahead(3) == '-') {
                parse_comment();
            } else if (lookahead(1) == '!' && lookahead(2) == '[' && 
                       lookahead(3) == 'C' && lookahead(4) == 'D' &&
                       lookahead(5) == 'A' && lookahead(6) == 'T' &&
                       lookahead(7) == 'A' && lookahead(8) == '[') {
                flush_text_buffer();
                parse_cdata();
            } else if (lookahead(1) == '!' && lookahead(2) == 'D' && lookahead(3) == 'O' && lookahead(4) == 'C') {
                flush_text_buffer();
                skip_dtd();
            } else if (lookahead(1) == '/') {
                flush_text_buffer();
                parse_end_element();
            } else {
                flush_text_buffer();
                parse_start_element();
            }
        } else {
            collect_text();
        }
    }
    
    void process_namespace_declaration(const Attribute& attr) {
        if (attr.name() == "xmlns") {
            namespace_stack_.push_back({"", attr.value()});
        } else if (attr.name().starts_with("xmlns:")) {
            std::string prefix = attr.name().substr(6);
            namespace_stack_.push_back({prefix, attr.value()});
        }
    }
    
    std::string resolve_namespace(std::string_view prefix) const {
        for (auto it = namespace_stack_.rbegin(); it != namespace_stack_.rend(); ++it) {
            if (it->prefix == prefix) {
                return it->uri;
            }
        }
        return "";
    }
    
    std::pair<std::string, std::string> parse_qname(std::string_view qname) {
        auto colon_pos = qname.find(':');
        if (colon_pos != std::string_view::npos) {
            return {std::string(qname.substr(0, colon_pos)), 
                    std::string(qname.substr(colon_pos + 1))};
        }
        return {"", std::string(qname)};
    }
    
    void flush_text_buffer() {
        if (text_buffer_.empty()) return;
        
        std::string text = std::string(text_buffer_.view());
        text_buffer_.clear();
        
        bool is_whitespace = std::ranges::all_of(text, [](char c) { 
            return std::isspace(static_cast<unsigned char>(c)) || 
                   c == '\xC2' || c == '\xA0';
        });
        
        if (is_whitespace && !config_.preserve_whitespace) {
            return;
        }
        
        EventType type = is_whitespace ? EventType::Whitespace : EventType::Characters;
        push_event(type, std::move(text));
    }
    
    void pop_element_stack() {
        if (!element_stack_.empty()) {
            element_stack_.pop();
            depth_ = element_stack_.size();
        }
    }
    
    void dispatch_event(ParseEvent event) {
        if (filter_ && !filter_(event)) {
            return;
        }
        
        event_queue_.push_back(event);
        
        if (callback_) {
            callback_(event);
        }
        
        if (handler_) {
            switch (event.type()) {
                case EventType::StartDocument:
                    handler_->on_start_document();
                    break;
                case EventType::EndDocument:
                    handler_->on_end_document();
                    break;
                case EventType::StartElement:
                    handler_->on_start_element(event.element());
                    break;
                case EventType::EndElement:
                    handler_->on_end_element(event.element());
                    break;
                case EventType::Characters:
                    handler_->on_characters(event.text());
                    break;
                case EventType::CDATA:
                    handler_->on_cdata(event.text());
                    break;
                case EventType::Comment:
                    handler_->on_comment(event.text());
                    break;
                case EventType::Whitespace:
                    handler_->on_whitespace(event.text());
                    break;
                case EventType::ProcessingInstruction:
                    handler_->on_processing_instruction(event.text(), "");
                    break;
                default:
                    break;
            }
        }
    }
    
    void push_event(EventType type) {
        ParseEvent event(type, current_location_);
        dispatch_event(std::move(event));
    }
    
    void push_event(EventType type, std::string data) {
        ParseEvent event(type, current_location_);
        event.set_text(std::move(data));
        dispatch_event(std::move(event));
    }
    
    void push_event(EventType type, ElementInfo elem) {
        ParseEvent event(type, current_location_);
        event.set_element(std::move(elem));
        dispatch_event(std::move(event));
    }
};

} // namespace litexml::stream