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

    // 将字符串拷贝到内存池中，返回 string_view
    std::string_view intern(std::string_view sv) {
        if (sv.empty()) return {};
        void* mem = allocate(sv.size(), 1);
        std::memcpy(mem, sv.data(), sv.size());
        return std::string_view(static_cast<char*>(mem), sv.size());
    }
    
    std::string_view intern(const std::string& str) {
        return intern(std::string_view(str));
    }
};

// ==================== DOM 节点定义 ====================
enum class NodeType : uint8_t {
    Document, Element, Text, CData, Comment, ProcessingInstruction
};

// 属性类型 - 使用 string_view 实现零拷贝
using AttributeMap = std::unordered_map<std::string_view, std::string_view>;

class DOMNode;
class ElementNode;
class TextNode;
class DocumentNode;

class DOMNode {
public:
    NodeType type;
    std::string_view nodeName;   // 零拷贝
    std::string_view nodeValue;  // 零拷贝
    DOMNode* parent{nullptr};
    std::vector<DOMNode*> children; // 使用原始指针，生命周期由 DocumentNode 统一管理
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
        return nodeValue.empty() || std::ranges::all_of(nodeValue, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
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
    DocumentAllocator allocator; // 内存池
    std::unique_ptr<std::string> m_ownedBuffer; // 用于 parseFile 时持有原始数据所有权

    static void destroyTree(DOMNode* node) {
        for (auto* child : node->children) {
            destroyTree(child);
            child->~DOMNode(); // 调用虚析构函数释放派生类资源
        }
    }

public:
    DocumentNode() : DOMNode(NodeType::Document) {
        nodeName = "#document";
        document = this;
    }
    
    ~DocumentNode() {
        destroyTree(this); // 统一销毁整棵树
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
        auto it = std::ranges::find_if(children, [](const auto& child) { return child->type == NodeType::Element; });
        if (it == children.end()) return nullptr;
        return static_cast<ElementNode*>(*it);
    }
    
    [[nodiscard]] auto getAllElements() const {
        return children
        | std::views::filter([](const auto& child) { return child->type == NodeType::Element; })
        | std::views::transform([](const auto& child) -> ElementNode* { return static_cast<ElementNode*>(child); });
    }
    
    DocumentAllocator& getAllocator() { return allocator; }
    
    void setOwnedBuffer(std::unique_ptr<std::string> buffer) { m_ownedBuffer = std::move(buffer); }
    std::string* getOwnedBuffer() { return m_ownedBuffer.get(); }
};

} // namespace litexml
#endif // LITEXML_DOM_NODE_H