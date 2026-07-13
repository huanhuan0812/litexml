// dom_node.h
#ifndef LITEXML_DOM_NODE_H
#define LITEXML_DOM_NODE_H

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <concepts>
#include <ranges>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <cctype>

namespace litexml {

// ==================== 内存池 (Arena Allocator) ====================
class DocumentAllocator {
    struct Block {
        std::unique_ptr<char[]> data;
        size_t size;
        size_t used;
    };
    std::vector<Block> blocks;
    static constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024; // 64KB 块大小

    void* allocate(size_t size, size_t alignment) {
        if (blocks.empty() || blocks.back().used + size + alignment > blocks.back().size) {
            size_t block_size = std::max(DEFAULT_BLOCK_SIZE, size + alignment);
            Block b;
            b.data = std::make_unique<char[]>(block_size);
            b.size = block_size;
            b.used = 0;
            blocks.push_back(std::move(b));
        }
        Block& current = blocks.back();
        size_t ptr_val = reinterpret_cast<size_t>(current.data.get() + current.used);
        size_t aligned_ptr_val = (ptr_val + alignment - 1) & ~(alignment - 1);
        size_t padding = aligned_ptr_val - ptr_val;
        
        if (current.used + padding + size > current.size) {
            size_t block_size = std::max(DEFAULT_BLOCK_SIZE, size + alignment);
            Block b;
            b.data = std::make_unique<char[]>(block_size);
            b.size = block_size;
            b.used = 0;
            blocks.push_back(std::move(b));
            return allocate(size, alignment);
        }
        current.used += padding;
        void* ptr = current.data.get() + current.used;
        current.used += size;
        return ptr;
    }

public:
    template<typename T, typename... Args>
    T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    std::string_view intern(std::string_view sv) {
        if (sv.empty()) return {};
        void* mem = allocate(sv.size(), 1);
        std::memcpy(mem, sv.data(), sv.size());
        return std::string_view(static_cast<char*>(mem), sv.size());
    }

    std::string_view intern(const std::string& str) {
        return intern(std::string_view(str));
    }

    std::string_view intern_with_size(std::string_view sv, size_t size_hint) {
        if (sv.empty()) return {};
        void* mem = allocate(size_hint, 1);
        std::memcpy(mem, sv.data(), sv.size());
        return std::string_view(static_cast<char*>(mem), sv.size());
    }
};

// ==================== DOM 节点定义 ====================
enum class NodeType : uint8_t {
    Document, Element, Text, CData, Comment, ProcessingInstruction
};

using AttributeMap = std::unordered_map<std::string_view, std::string_view>;

class DOMNode;
class ElementNode;
class TextNode;
class DocumentNode;

class DOMNode {
public:
    NodeType type;
    std::string_view nodeName;   
    std::string_view nodeValue;  
    DOMNode* parent{nullptr};
    std::vector<DOMNode*> children; 
    DocumentNode* document{nullptr};

    explicit DOMNode(NodeType t = NodeType::Element) noexcept : type(t) {}
    virtual ~DOMNode() = default;
    
    DOMNode(const DOMNode&) = delete;
    DOMNode& operator=(const DOMNode&) = delete;
    DOMNode(DOMNode&&) = default;
    DOMNode& operator=(DOMNode&&) = default;

    template<typename T>
    requires std::derived_from<T, DOMNode>
    T* appendChild(T* child) {
        child->parent = this;
        child->document = this->document;
        children.push_back(child);
        return child;
    }

    [[nodiscard]] std::optional<DOMNode*> getFirstChild() const noexcept {
        if (children.empty()) return std::nullopt;
        return children.front();
    }

    [[nodiscard]] std::optional<DOMNode*> getLastChild() const noexcept {
        if (children.empty()) return std::nullopt;
        return children.back();
    }

    // ==================== 新增：DOM 遍历方法 ====================
    [[nodiscard]] DOMNode* getParent() const noexcept {
        return parent;
    }

    [[nodiscard]] std::optional<DOMNode*> getNextSibling() const noexcept {
        if (!parent) return std::nullopt;
        auto& siblings = parent->children;
        for (size_t i = 0; i < siblings.size(); ++i) {
            if (siblings[i] == this && i + 1 < siblings.size()) {
                return siblings[i + 1];
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DOMNode*> getPreviousSibling() const noexcept {
        if (!parent) return std::nullopt;
        auto& siblings = parent->children;
        for (size_t i = 1; i < siblings.size(); ++i) {
            if (siblings[i] == this) {
                return siblings[i - 1];
            }
        }
        return std::nullopt;
    }

    // ==================== 新增：DOM 修改方法 ====================
    bool removeChild(DOMNode* child) {
        if (!child) return false;
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end()) {
            children.erase(it);
            child->parent = nullptr;
            child->~DOMNode(); // 手动调用虚析构函数释放派生类资源
            return true;
        }
        return false;
    }

    bool replaceChild(DOMNode* newChild, DOMNode* oldChild) {
        if (!newChild || !oldChild) return false;
        auto it = std::find(children.begin(), children.end(), oldChild);
        if (it != children.end()) {
            *it = newChild;
            newChild->parent = this;
            newChild->document = this->document;
            
            oldChild->parent = nullptr;
            oldChild->~DOMNode();
            return true;
        }
        return false;
    }

    template<typename Predicate>
    [[nodiscard]] std::optional<DOMNode*> findChild(Predicate pred) const {
        auto it = std::ranges::find_if(children, pred);
        if (it == children.end()) return std::nullopt;
        return *it;
    }

    template<NodeType Type>
    [[nodiscard]] auto getChildrenByType() const {
        return children | std::views::filter([](const auto& child) { return child->type == Type; });
    }
};

class ElementNode : public DOMNode {
private:
    AttributeMap m_attributes;
public:
    explicit ElementNode(std::string_view name) : DOMNode(NodeType::Element) {
        nodeName = name;
    }
    
    ~ElementNode() override = default; 

    [[nodiscard]] std::string_view getTagName() const noexcept { return nodeName; }

    [[nodiscard]] std::optional<std::string_view> getAttribute(std::string_view name) const {
        auto it = m_attributes.find(name);
        if (it == m_attributes.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::string_view getAttributeOrDefault(std::string_view name, std::string_view defaultValue = "") const {
        auto opt = getAttribute(name);
        return opt.value_or(defaultValue);
    }

    void setAttribute(std::string_view name, std::string_view value) {
        m_attributes.emplace(name, value);
    }

    [[nodiscard]] const AttributeMap& getAttributes() const noexcept { return m_attributes; }
    [[nodiscard]] auto begin() const { return m_attributes.begin(); }
    [[nodiscard]] auto end() const { return m_attributes.end(); }
};

class TextNode : public DOMNode {
public:
    explicit TextNode(std::string_view text) : DOMNode(NodeType::Text) { nodeValue = text; }
    [[nodiscard]] std::string_view getText() const noexcept { return nodeValue; }
    [[nodiscard]] bool isEmpty() const noexcept {
        return nodeValue.empty() || std::ranges::all_of(nodeValue, [](char c) { 
            return std::isspace(static_cast<unsigned char>(c)); 
        });
    }
};

class CDataNode : public DOMNode {
public:
    explicit CDataNode(std::string_view data) : DOMNode(NodeType::CData) { nodeValue = data; }
    [[nodiscard]] std::string_view getData() const noexcept { return nodeValue; }
};

class CommentNode : public DOMNode {
public:
    explicit CommentNode(std::string_view comment) : DOMNode(NodeType::Comment) { nodeValue = comment; }
    [[nodiscard]] std::string_view getComment() const noexcept { return nodeValue; }
};

class DocumentNode : public DOMNode {
private:
    std::string_view m_encoding;
    std::string_view m_version;
    DocumentAllocator allocator; 
    std::unique_ptr<std::string> m_ownedBuffer; 

    static void destroyTree(DOMNode* node) {
        for (auto* child : node->children) {
            destroyTree(child);
            child->~DOMNode(); 
        }
    }

    // 查询辅助方法
    void findElementsById(const DOMNode* node, std::string_view id, ElementNode*& result) const {
        if (!node || result) return;
        if (node->type == NodeType::Element) {
            auto* elem = static_cast<const ElementNode*>(node);
            auto attr = elem->getAttribute("id");
            if (attr.has_value() && attr.value() == id) {
                result = const_cast<ElementNode*>(elem);
                return;
            }
        }
        for (auto* child : node->children) {
            findElementsById(child, id, result);
            if (result) return; 
        }
    }

    void findElementsByTagName(const DOMNode* node, std::string_view tagName, std::vector<ElementNode*>& result) const {
        if (!node) return;
        if (node->type == NodeType::Element) {
            auto* elem = static_cast<const ElementNode*>(node);
            if (tagName == "*" || elem->getTagName() == tagName) {
                result.push_back(const_cast<ElementNode*>(elem));
            }
        }
        for (auto* child : node->children) {
            findElementsByTagName(child, tagName, result);
        }
    }

public:
    DocumentNode() : DOMNode(NodeType::Document) {
        nodeName = "#document";
        document = this;
    }
    
    ~DocumentNode() {
        destroyTree(this); 
    }

    void setEncoding(std::string_view encoding) { m_encoding = encoding; }
    void setVersion(std::string_view version) { m_version = version; }

    [[nodiscard]] std::optional<std::string_view> getEncoding() const noexcept {
        return m_encoding.empty() ? std::nullopt : std::optional(m_encoding);
    }

    [[nodiscard]] std::optional<std::string_view> getVersion() const noexcept {
        return m_version.empty() ? std::nullopt : std::optional(m_version);
    }

    [[nodiscard]] ElementNode* getDocumentElement() const {
        auto it = std::ranges::find_if(children, [](const auto& child) { 
            return child->type == NodeType::Element; 
        });
        if (it == children.end()) return nullptr;
        return static_cast<ElementNode*>(*it);
    }

    [[nodiscard]] auto getAllElements() const {
        return children
        | std::views::filter([](const auto& child) { return child->type == NodeType::Element; })
        | std::views::transform([](const auto& child) -> ElementNode* { return static_cast<ElementNode*>(child); });
    }

    // ==================== 新增：DOM 查询方法 ====================
    [[nodiscard]] ElementNode* getElementById(std::string_view id) const {
        ElementNode* result = nullptr;
        findElementsById(this, id, result);
        return result;
    }

    [[nodiscard]] std::vector<ElementNode*> getElementsByTagName(std::string_view tagName) const {
        std::vector<ElementNode*> result;
        findElementsByTagName(this, tagName, result);
        return result;
    }

    DocumentAllocator& getAllocator() { return allocator; }
    void setOwnedBuffer(std::unique_ptr<std::string> buffer) { m_ownedBuffer = std::move(buffer); }
    std::string* getOwnedBuffer() { return m_ownedBuffer.get(); }
};

} // namespace litexml
#endif // LITEXML_DOM_NODE_H