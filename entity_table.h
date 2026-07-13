// entity_table.h
#ifndef LITEXML_ENTITY_TABLE_H
#define LITEXML_ENTITY_TABLE_H

#include <string_view>
#include <optional>
#include <cstdint>

namespace litexml {

class DocumentAllocator;

// 高度优化的实体表 - 零开销抽象
class EntityTable {
public:
    // 内联查找，避免函数调用开销
    static inline std::optional<char> lookup(std::string_view entity) noexcept {
        // 快速长度检查（所有实体都是 4-6 个字符）
        size_t len = entity.size();
        if (len < 4 || len > 6 || entity[0] != '&' || entity.back() != ';') {
            return std::nullopt;
        }
        
        // 基于第二个字符的 switch 跳转
        switch (entity[1]) {
            case 'a': // &amp; 或 &apos;
                if (len == 5) { // &amp;
                    if (entity[2] == 'm' && entity[3] == 'p') return '&';
                } else if (len == 6) { // &apos;
                    if (entity[2] == 'p' && entity[3] == 'o' && entity[4] == 's') return '\'';
                }
                break;
            case 'l': // &lt;
                if (len == 4 && entity[2] == 't') return '<';
                break;
            case 'g': // &gt;
                if (len == 4 && entity[2] == 't') return '>';
                break;
            case 'q': // &quot;
                if (len == 6 && entity[2] == 'u' && entity[3] == 'o' && entity[4] == 't') return '"';
                break;
            default:
                break;
        }
        return std::nullopt;
    }
    
    // 快速检测是否包含实体
    static inline bool has_entity(std::string_view text) noexcept {
        return text.find('&') != std::string_view::npos;
    }
    
    // 快速检测实体并解码（单次遍历）
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
            
            // 复制实体前的文本
            if (entity_pos > pos) {
                result.append(text.data() + pos, entity_pos - pos);
            }
            
            // 查找实体结束
            size_t end_pos = text.find(';', entity_pos);
            if (end_pos == std::string_view::npos || end_pos - entity_pos > 6) {
                // 不是有效实体，保留 '&'
                result.push_back('&');
                pos = entity_pos + 1;
                continue;
            }
            
            std::string_view entity = text.substr(entity_pos, end_pos - entity_pos + 1);
            auto replacement = lookup(entity);
            if (replacement.has_value()) {
                result.push_back(replacement.value());
            } else {
                // 未知实体，保留原样
                result.append(entity.data(), entity.size());
            }
            
            pos = end_pos + 1;
        }
        
        return result;
    }
};

// 轻量级文本累加器 - 复用缓冲区
class TextAccumulator {
private:
    std::string buffer;
    DocumentAllocator* allocator;
    bool has_content{false};
    
public:
    explicit TextAccumulator(DocumentAllocator* alloc) 
        : allocator(alloc) {
        buffer.reserve(1024);  // 预分配较大缓冲区
    }
    
    // 追加字符
    inline void append(char c) noexcept {
        buffer.push_back(c);
        has_content = true;
    }
    
    // 追加字符串片段
    inline void append(std::string_view sv) noexcept {
        if (!sv.empty()) {
            buffer.append(sv.data(), sv.size());
            has_content = true;
        }
    }
    
    // 追加实体（自动解码）
    inline void append_entity(std::string_view entity) noexcept {
        has_content = true;
        auto replacement = EntityTable::lookup(entity);
        if (replacement.has_value()) {
            buffer.push_back(replacement.value());
        } else {
            buffer.append(entity.data(), entity.size());
        }
    }
    
    // 完成累积，返回池化的字符串视图
    inline std::string_view finalize() noexcept {
        if (buffer.empty()) return {};
        return allocator->intern(buffer);
    }
    
    // 检查是否有内容
    inline bool empty() const noexcept { return !has_content; }
    
    // 重置累加器
    inline void clear() noexcept {
        buffer.clear();
        has_content = false;
    }
    
    // 预分配空间
    inline void reserve(size_t size) {
        buffer.reserve(size);
    }
    
    // 获取缓冲区大小（用于调试）
    inline size_t size() const noexcept { return buffer.size(); }
};

} // namespace litexml

#endif // LITEXML_ENTITY_TABLE_H