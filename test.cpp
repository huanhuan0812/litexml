// test.cpp
#include "xml_parser.h"
#include "xml_serializer.h"
#include <iostream>

using namespace litexml;

int main() {
    std::string_view xml_data = R"(
        <?xml version="1.0" encoding="UTF-8"?>
        <library>
            <book id="bk101" category="fiction">
                <title>XML Developer's Guide</title>
                <price>44.95</price>
            </book>
            <book id="bk102" category="fantasy">
                <title>Midnight Rain</title>
                <price>5.95</price>
            </book>
        </library>
    )";

    XMLParser parser;
    auto result = parser.parse(xml_data);
    if (!result) {
        std::cerr << "Parse error: " << result.error().toString() << "\n";
        return 1;
    }
    auto doc = std::move(result.value());

    // 1. DOM 查询方法
    std::cout << "--- Querying DOM ---\n";
    auto* targetBook = doc->getElementById("bk102");
    if (targetBook) {
        std::cout << "Found book by ID: " << targetBook->getAttribute("id").value() << "\n";
    }

    auto allBooks = doc->getElementsByTagName("book");
    std::cout << "Total <book> elements: " << allBooks.size() << "\n";

    // 2. DOM 遍历方法
    std::cout << "\n--- Traversing DOM ---\n";
    if (targetBook) {
        if (auto prev = targetBook->getPreviousSibling()) {
            while (*prev && (*prev)->type != NodeType::Element) {
                prev = (*prev)->getPreviousSibling();
            }
            if (*prev) {
                auto* prevElem = static_cast<ElementNode*>(*prev);
                std::cout << "Previous sibling tag: " << prevElem->getTagName() 
                          << " (id: " << prevElem->getAttribute("id").value_or("none") << ")\n";
            }
        }
        
        // 【修复】：getParent() 返回 DOMNode*，直接使用 -> 访问
        if (auto* parent = targetBook->getParent()) {
            std::cout << "Parent tag: " << parent->nodeName << "\n";
        }
    }

    // 3. DOM 修改方法
    std::cout << "\n--- Modifying DOM ---\n";
    auto* firstBook = allBooks[0];
    auto* libraryNode = firstBook->getParent();
    if (libraryNode) {
        libraryNode->removeChild(firstBook);
        std::cout << "Removed first book.\n";
    }

    // 4. XML 序列化
    std::cout << "\n--- Serialized XML ---\n";
    XMLSerializer::Config config;
    config.prettyPrint = true;
    config.indentString = "    "; 
    XMLSerializer serializer(config);
    
    std::string output = serializer.serialize(doc.get());
    std::cout << output << "\n";

    return 0;
}