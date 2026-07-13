// entity_table.h
#ifndef LITEXML_ENTITY_TABLE_H
#define LITEXML_ENTITY_TABLE_H

#include "dom_node.h"
#include <string_view>
#include <optional>
#include <string>
#include <unordered_map>

namespace litexml {

class DocumentAllocator;

// 高度优化的实体表 - 零开销抽象
class EntityTable {
public:
    // 数值实体解析
    static inline bool parseNumericEntity(std::string_view entity, char32_t& out) noexcept {
        // 检查基本格式: &#...;
        if (entity.size() < 4) {
            return false;
        }
        if (entity[0] != '&') {
            return false;
        }
        if (entity[entity.size() - 1] != ';') {
            return false;
        }
        
        // 提取 & 和 ; 之间的内容
        std::string_view content = entity.substr(1, entity.size() - 2);
        
        if (content.empty() || content[0] != '#') {
            return false;
        }
        
        if (content.size() < 2) {
            return false;
        }
        
        // 检查是否十六进制格式
        if (content[1] == 'x' || content[1] == 'X') {
            if (content.size() < 3) {
                return false;
            }
            char32_t value = 0;
            for (size_t i = 2; i < content.size(); ++i) {
                char c = content[i];
                if (c >= '0' && c <= '9') {
                    value = value * 16 + (c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    value = value * 16 + (c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    value = value * 16 + (c - 'A' + 10);
                } else {
                    return false;
                }
            }
            out = value;
            return true;
        } else {
            // 十进制格式
            char32_t value = 0;
            for (size_t i = 1; i < content.size(); ++i) {
                char c = content[i];
                if (c >= '0' && c <= '9') {
                    value = value * 10 + (c - '0');
                } else {
                    return false;
                }
            }
            out = value;
            return true;
        }
    }

public:
    // 将 Unicode 码点编码为 UTF-8
    static inline void appendCodepoint(std::string& out, char32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x110000) {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    
    // 命名字体查找
    static inline std::optional<char> lookup(std::string_view entity) noexcept {
        size_t len = entity.size();
        if (len < 4 || len > 6 || entity[0] != '&' || entity[len - 1] != ';') {
            return std::nullopt;
        }
        
        switch (entity[1]) {
            case 'a':
                if (len == 5 && entity[2] == 'm' && entity[3] == 'p') return '&';
                if (len == 6 && entity[2] == 'p' && entity[3] == 'o' && entity[4] == 's') return '\'';
                break;
            case 'l':
                if (len == 4 && entity[2] == 't') return '<';
                break;
            case 'g':
                if (len == 4 && entity[2] == 't') return '>';
                break;
            case 'q':
                if (len == 6 && entity[2] == 'u' && entity[3] == 'o' && entity[4] == 't') return '"';
                break;
            default:
                break;
        }
        return std::nullopt;
    }
    
    // 查找扩展实体
    static inline std::optional<std::string_view> lookupExtended(std::string_view entity) noexcept {
        static const std::unordered_map<std::string_view, std::string_view> extended_entities = {
            {"&nbsp;", "\xC2\xA0"},
            {"&iexcl;", "¡"},
            {"&cent;", "¢"},
            {"&pound;", "£"},
            {"&curren;", "¤"},
            {"&yen;", "¥"},
            {"&brvbar;", "¦"},
            {"&sect;", "§"},
            {"&uml;", "¨"},
            {"&copy;", "©"},
            {"&ordf;", "ª"},
            {"&laquo;", "«"},
            {"&not;", "¬"},
            {"&shy;", ""},
            {"&reg;", "®"},
            {"&macr;", "¯"},
            {"&deg;", "°"},
            {"&plusmn;", "±"},
            {"&sup2;", "²"},
            {"&sup3;", "³"},
            {"&acute;", "´"},
            {"&micro;", "µ"},
            {"&para;", "¶"},
            {"&middot;", "·"},
            {"&cedil;", "¸"},
            {"&sup1;", "¹"},
            {"&ordm;", "º"},
            {"&raquo;", "»"},
            {"&frac14;", "¼"},
            {"&frac12;", "½"},
            {"&frac34;", "¾"},
            {"&iquest;", "¿"},
            {"&Agrave;", "À"},
            {"&Aacute;", "Á"},
            {"&Acirc;", "Â"},
            {"&Atilde;", "Ã"},
            {"&Auml;", "Ä"},
            {"&Aring;", "Å"},
            {"&AElig;", "Æ"},
            {"&Ccedil;", "Ç"},
            {"&Egrave;", "È"},
            {"&Eacute;", "É"},
            {"&Ecirc;", "Ê"},
            {"&Euml;", "Ë"},
            {"&Igrave;", "Ì"},
            {"&Iacute;", "Í"},
            {"&Icirc;", "Î"},
            {"&Iuml;", "Ï"},
            {"&ETH;", "Ð"},
            {"&Ntilde;", "Ñ"},
            {"&Ograve;", "Ò"},
            {"&Oacute;", "Ó"},
            {"&Ocirc;", "Ô"},
            {"&Otilde;", "Õ"},
            {"&Ouml;", "Ö"},
            {"&times;", "×"},
            {"&Oslash;", "Ø"},
            {"&Ugrave;", "Ù"},
            {"&Uacute;", "Ú"},
            {"&Ucirc;", "Û"},
            {"&Uuml;", "Ü"},
            {"&Yacute;", "Ý"},
            {"&THORN;", "Þ"},
            {"&szlig;", "ß"},
            {"&agrave;", "à"},
            {"&aacute;", "á"},
            {"&acirc;", "â"},
            {"&atilde;", "ã"},
            {"&auml;", "ä"},
            {"&aring;", "å"},
            {"&aelig;", "æ"},
            {"&ccedil;", "ç"},
            {"&egrave;", "è"},
            {"&eacute;", "é"},
            {"&ecirc;", "ê"},
            {"&euml;", "ë"},
            {"&igrave;", "ì"},
            {"&iacute;", "í"},
            {"&icirc;", "î"},
            {"&iuml;", "ï"},
            {"&eth;", "ð"},
            {"&ntilde;", "ñ"},
            {"&ograve;", "ò"},
            {"&oacute;", "ó"},
            {"&ocirc;", "ô"},
            {"&otilde;", "õ"},
            {"&ouml;", "ö"},
            {"&divide;", "÷"},
            {"&oslash;", "ø"},
            {"&ugrave;", "ù"},
            {"&uacute;", "ú"},
            {"&ucirc;", "û"},
            {"&uuml;", "ü"},
            {"&yacute;", "ý"},
            {"&thorn;", "þ"},
            {"&yuml;", "ÿ"},
            {"&Alpha;", "Α"},
            {"&Beta;", "Β"},
            {"&Gamma;", "Γ"},
            {"&Delta;", "Δ"},
            {"&Epsilon;", "Ε"},
            {"&Zeta;", "Ζ"},
            {"&Eta;", "Η"},
            {"&Theta;", "Θ"},
            {"&Iota;", "Ι"},
            {"&Kappa;", "Κ"},
            {"&Lambda;", "Λ"},
            {"&Mu;", "Μ"},
            {"&Nu;", "Ν"},
            {"&Xi;", "Ξ"},
            {"&Omicron;", "Ο"},
            {"&Pi;", "Π"},
            {"&Rho;", "Ρ"},
            {"&Sigma;", "Σ"},
            {"&Tau;", "Τ"},
            {"&Upsilon;", "Υ"},
            {"&Phi;", "Φ"},
            {"&Chi;", "Χ"},
            {"&Psi;", "Ψ"},
            {"&Omega;", "Ω"},
            {"&alpha;", "α"},
            {"&beta;", "β"},
            {"&gamma;", "γ"},
            {"&delta;", "δ"},
            {"&epsilon;", "ε"},
            {"&zeta;", "ζ"},
            {"&eta;", "η"},
            {"&theta;", "θ"},
            {"&iota;", "ι"},
            {"&kappa;", "κ"},
            {"&lambda;", "λ"},
            {"&mu;", "μ"},
            {"&nu;", "ν"},
            {"&xi;", "ξ"},
            {"&omicron;", "ο"},
            {"&pi;", "π"},
            {"&rho;", "ρ"},
            {"&sigmaf;", "ς"},
            {"&sigma;", "σ"},
            {"&tau;", "τ"},
            {"&upsilon;", "υ"},
            {"&phi;", "φ"},
            {"&chi;", "χ"},
            {"&psi;", "ψ"},
            {"&omega;", "ω"},
            {"&bull;", "•"},
            {"&hellip;", "…"},
            {"&prime;", "′"},
            {"&Prime;", "″"},
            {"&oline;", "‾"},
            {"&frasl;", "⁄"},
            {"&euro;", "€"},
            {"&larr;", "←"},
            {"&uarr;", "↑"},
            {"&rarr;", "→"},
            {"&darr;", "↓"},
            {"&harr;", "↔"},
            {"&forall;", "∀"},
            {"&part;", "∂"},
            {"&exist;", "∃"},
            {"&empty;", "∅"},
            {"&nabla;", "∇"},
            {"&isin;", "∈"},
            {"&notin;", "∉"},
            {"&ni;", "∋"},
            {"&prod;", "∏"},
            {"&sum;", "∑"},
            {"&minus;", "−"},
            {"&lowast;", "∗"},
            {"&radic;", "√"},
            {"&prop;", "∝"},
            {"&infin;", "∞"},
            {"&ang;", "∠"},
            {"&and;", "∧"},
            {"&or;", "∨"},
            {"&cap;", "∩"},
            {"&cup;", "∪"},
            {"&int;", "∫"},
            {"&there4;", "∴"},
            {"&sim;", "∼"},
            {"&cong;", "≅"},
            {"&asymp;", "≈"},
            {"&ne;", "≠"},
            {"&equiv;", "≡"},
            {"&le;", "≤"},
            {"&ge;", "≥"},
            {"&sub;", "⊂"},
            {"&sup;", "⊃"},
            {"&nsub;", "⊄"},
            {"&sube;", "⊆"},
            {"&supe;", "⊇"},
            {"&oplus;", "⊕"},
            {"&otimes;", "⊗"},
            {"&perp;", "⊥"},
            {"&sdot;", "⋅"},
            {"&lceil;", "⌈"},
            {"&rceil;", "⌉"},
            {"&lfloor;", "⌊"},
            {"&rfloor;", "⌋"},
            {"&lang;", "⟨"},
            {"&rang;", "⟩"},
            {"&ensp;", " "},
            {"&emsp;", " "},
            {"&thinsp;", " "},
            {"&zwnj;", ""},
            {"&zwj;", ""},
            {"&lrm;", ""},
            {"&rlm;", ""},
        };
        
        auto it = extended_entities.find(entity);
        if (it != extended_entities.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    // 快速检测是否包含有效实体
    static inline bool has_entity(std::string_view text) noexcept {
        size_t pos = text.find('&');
        if (pos == std::string_view::npos) {
            return false;
        }
        
        while (pos != std::string_view::npos) {
            size_t end_pos = text.find(';', pos + 1);
            if (end_pos != std::string_view::npos) {
                size_t len = end_pos - pos;
                if (len >= 3 && len <= 10) {
                    std::string_view entity = text.substr(pos, len + 1);
                    
                    if (entity[1] == '#') {
                        if (entity.size() >= 4) {
                            if (entity[2] == 'x' || entity[2] == 'X') {
                                if (entity.size() >= 5) {
                                    bool has_digit = false;
                                    for (size_t i = 3; i < entity.size() - 1; ++i) {
                                        char c = entity[i];
                                        if ((c >= '0' && c <= '9') || 
                                            (c >= 'a' && c <= 'f') || 
                                            (c >= 'A' && c <= 'F')) {
                                            has_digit = true;
                                            break;
                                        }
                                    }
                                    if (has_digit) return true;
                                }
                            } else {
                                bool has_digit = false;
                                for (size_t i = 2; i < entity.size() - 1; ++i) {
                                    if (entity[i] >= '0' && entity[i] <= '9') {
                                        has_digit = true;
                                        break;
                                    }
                                }
                                if (has_digit) return true;
                            }
                        }
                    } else {
                        bool has_letter = false;
                        for (size_t i = 1; i < entity.size() - 1; ++i) {
                            if (std::isalpha(static_cast<unsigned char>(entity[i]))) {
                                has_letter = true;
                                break;
                            }
                        }
                        if (has_letter) {
                            return true;
                        }
                    }
                }
            }
            pos = text.find('&', pos + 1);
        }
        return false;
    }
    
    // 解码实体
    static inline std::string decode_entities(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        
        size_t pos = 0;
        while (pos < text.size()) {
            size_t entity_pos = text.find('&', pos);
            if (entity_pos == std::string_view::npos) {
                result.append(text.data() + pos, text.size() - pos);
                break;
            }
            
            if (entity_pos > pos) {
                result.append(text.data() + pos, entity_pos - pos);
            }
            
            size_t end_pos = text.find(';', entity_pos);
            if (end_pos == std::string_view::npos || end_pos - entity_pos > 12) {
                result.push_back('&');
                pos = entity_pos + 1;
                continue;
            }
            
            std::string_view entity = text.substr(entity_pos, end_pos - entity_pos + 1);
            
            bool decoded = false;
            
            // 1. 尝试数值实体
            char32_t codepoint = 0;
            if (parseNumericEntity(entity, codepoint)) {
                appendCodepoint(result, codepoint);
                decoded = true;
            }
            
            // 2. 尝试命名字体
            if (!decoded) {
                auto named = lookup(entity);
                if (named.has_value()) {
                    result.push_back(named.value());
                    decoded = true;
                }
            }
            
            // 3. 尝试扩展命名字体
            if (!decoded) {
                auto extended = lookupExtended(entity);
                if (extended.has_value()) {
                    result.append(extended.value().data(), extended.value().size());
                    decoded = true;
                }
            }
            
            // 4. 未知实体，保留原样
            if (!decoded) {
                result.append(entity.data(), entity.size());
            }
            
            pos = end_pos + 1;
        }
        
        return result;
    }
};

// 轻量级文本累加器
class TextAccumulator {
private:
    std::string buffer;
    DocumentAllocator* allocator;
    bool has_content{false};
    
public:
    explicit TextAccumulator(DocumentAllocator* alloc) 
        : allocator(alloc) {
        buffer.reserve(1024);
    }
    
    inline void append(char c) noexcept {
        buffer.push_back(c);
        has_content = true;
    }
    
    inline void append(std::string_view sv) noexcept {
        if (!sv.empty()) {
            buffer.append(sv.data(), sv.size());
            has_content = true;
        }
    }
    
    inline void append_entity(std::string_view entity) noexcept {
        has_content = true;
        
        char32_t codepoint = 0;
        if (EntityTable::parseNumericEntity(entity, codepoint)) {
            EntityTable::appendCodepoint(buffer, codepoint);
            return;
        }
        
        auto named = EntityTable::lookup(entity);
        if (named.has_value()) {
            buffer.push_back(named.value());
            return;
        }
        
        auto extended = EntityTable::lookupExtended(entity);
        if (extended.has_value()) {
            buffer.append(extended.value().data(), extended.value().size());
            return;
        }
        
        buffer.append(entity.data(), entity.size());
    }
    
    inline std::string_view finalize() noexcept {
        if (buffer.empty()) return {};
        return allocator->intern(buffer);
    }
    
    inline bool empty() const noexcept { return !has_content; }
    
    inline void clear() noexcept {
        buffer.clear();
        has_content = false;
    }
    
    inline void reserve(size_t size) {
        buffer.reserve(size);
    }
    
    inline size_t size() const noexcept { return buffer.size(); }
};

} // namespace litexml

#endif // LITEXML_ENTITY_TABLE_H