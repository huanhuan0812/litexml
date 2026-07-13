#include "xml_parser.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <chrono>
#include <filesystem>

using namespace litexml;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAILED] " << __FILE__ << ":" << __LINE__ << " - " << message << std::endl; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(func) \
    do { \
        std::cout << "Running test: " << #func << "... "; \
        if (func()) { \
            std::cout << "PASSED" << std::endl; \
        } else { \
            std::cout << "FAILED" << std::endl; \
            return 1; \
        } \
    } while (0)

bool test_basic_parsing() {
    std::string xml = R"(
        <root id="1">
            <child name="test">Hello World</child>
            <empty/>
        </root>
    )";

    XMLParser parser;
    auto result = parser.parse(xml);
    TEST_ASSERT(result.has_value(), "Parsing should succeed");

    auto doc = std::move(result.value());
    auto* root = doc->getDocumentElement();
    TEST_ASSERT(root != nullptr, "Root element should exist");
    TEST_ASSERT(root->getTagName() == "root", "Root tag should be 'root'");
    TEST_ASSERT(root->getAttribute("id").value_or("") == "1", "Root id should be '1'");

    // 修复：children 元素已经是 DOMNode*，无需 .get()
    auto* child = static_cast<ElementNode*>(root->children[0]);
    TEST_ASSERT(child->getTagName() == "child", "Child tag should be 'child'");
    TEST_ASSERT(child->getAttribute("name").value_or("") == "test", "Child name should be 'test'");
    
    auto* text = static_cast<TextNode*>(child->children[0]);
    TEST_ASSERT(text->getText() == "Hello World", "Text should be 'Hello World'");

    auto* empty = static_cast<ElementNode*>(root->children[1]);
    TEST_ASSERT(empty->getTagName() == "empty", "Empty tag should be 'empty'");
    TEST_ASSERT(empty->children.empty(), "Empty element should have no children");

    return true;
}

bool test_zero_copy_attributes() {
    std::string xml = "<root attr=\"value\" />";
    
    XMLParser parser;
    auto result = parser.parse(xml);
    TEST_ASSERT(result.has_value(), "Parsing should succeed");
    
    auto doc = std::move(result.value());
    auto* root = doc->getDocumentElement();
    
    auto attrVal = root->getAttribute("attr");
    TEST_ASSERT(attrVal.has_value(), "Attribute should exist");
    
    const char* xmlStart = xml.data();
    const char* xmlEnd = xml.data() + xml.size();
    const char* attrStart = attrVal->data();
    
    TEST_ASSERT(attrStart >= xmlStart && attrStart < xmlEnd, 
                "Attribute value should point to original buffer (Zero-Copy)");
                
    return true;
}

bool test_entity_escaping_and_pooling() {
    std::string xml = "<root>&lt;tag&gt; &amp; &quot;quote&quot;</root>";
    
    XMLParser parser;
    auto result = parser.parse(xml);
    TEST_ASSERT(result.has_value(), "Parsing should succeed");
    
    auto doc = std::move(result.value());
    auto* root = doc->getDocumentElement();
    auto* text = static_cast<TextNode*>(root->children[0]);
    
    std::string expected = "<tag> & \"quote\"";
    TEST_ASSERT(text->getText() == expected, "Entities should be unescaped correctly");
    
    const char* xmlStart = xml.data();
    const char* xmlEnd = xml.data() + xml.size();
    const char* textStart = text->getText().data();
    
    bool isOutsideOriginal = (textStart < xmlStart || textStart >= xmlEnd);
    TEST_ASSERT(isOutsideOriginal, "Unescaped text should be allocated in Memory Pool (Arena)");

    return true;
}

bool test_cdata_and_comments() {
    std::string xml = R"(
        <root>
            <!-- This is a comment -->
            <![CDATA[<b>Bold</b> & <i>Italic</i>]]>
            <?pi target?>
            <child>Text</child>
        </root>
    )";

    XMLParser::Config config;
    config.parseComments = true;
    config.parseProcessingInstructions = true;
    XMLParser parser(config);

    auto result = parser.parse(xml);
    TEST_ASSERT(result.has_value(), "Parsing should succeed");

    auto doc = std::move(result.value());
    auto* root = doc->getDocumentElement();

    auto* comment = static_cast<CommentNode*>(root->children[0]);
    TEST_ASSERT(comment->type == NodeType::Comment, "First child should be Comment");
    TEST_ASSERT(comment->getComment() == " This is a comment ", "Comment content mismatch");

    auto* cdata = static_cast<CDataNode*>(root->children[1]);
    TEST_ASSERT(cdata->type == NodeType::CData, "Second child should be CDATA");
    TEST_ASSERT(cdata->getData() == "<b>Bold</b> & <i>Italic</i>", "CDATA content mismatch");

    auto* lastChild = static_cast<ElementNode*>(root->children.back());
    TEST_ASSERT(lastChild->getTagName() == "child", "Last child should be 'child'");

    return true;
}

bool test_error_handling() {
    XMLParser parser;

    auto res1 = parser.parse("<root></wrong>");
    TEST_ASSERT(!res1.has_value(), "Should fail on mismatched tags");
    TEST_ASSERT(res1.error().error == ParseError::MismatchedTag, "Error type should be MismatchedTag");

    auto res2 = parser.parse("<root attr=>");
    TEST_ASSERT(!res2.has_value(), "Should fail on invalid attribute");

    auto res3 = parser.parse("<root>");
    TEST_ASSERT(!res3.has_value(), "Should fail on unclosed tag");

    return true;
}

bool test_file_parsing_and_ownership() {
    std::string filename = "test_temp.xml";
    std::string content = "<root><item id='1'>File Test</item></root>";
    
    {
        std::ofstream ofs(filename);
        ofs << content;
    }

    XMLParser parser;
    auto result = parser.parseFile(filename);
    std::filesystem::remove(filename);

    TEST_ASSERT(result.has_value(), "File parsing should succeed");
    
    auto doc = std::move(result.value());
    auto* root = doc->getDocumentElement();
    TEST_ASSERT(root->getTagName() == "root", "Root tag mismatch");
    
    auto* item = static_cast<ElementNode*>(root->children[0]);
    TEST_ASSERT(item->getAttribute("id").value_or("") == "1", "Attribute mismatch");
    
    auto* text = static_cast<TextNode*>(item->children[0]);
    TEST_ASSERT(text->getText() == "File Test", "Text mismatch");

    TEST_ASSERT(doc->getOwnedBuffer() != nullptr, "Document should own the file buffer");
    TEST_ASSERT(*doc->getOwnedBuffer() == content, "Owned buffer content should match");

    const std::string& buffer = *doc->getOwnedBuffer();
    auto attrVal = item->getAttribute("id");
    TEST_ASSERT(attrVal.has_value(), "Attribute should exist");
    
    const char* bufStart = buffer.data();
    const char* bufEnd = buffer.data() + buffer.size();
    const char* attrPtr = attrVal->data();
    
    TEST_ASSERT(attrPtr >= bufStart && attrPtr < bufEnd, 
                "Attribute should point to owned file buffer (Zero-Copy)");

    return true;
}

bool test_memory_pool_efficiency() {
    std::string xml = "<root>";
    for (int i = 0; i < 1000; ++i) {
        xml += "<item id='" + std::to_string(i) + "'>Value" + std::to_string(i) + "</item>";
    }
    xml += "</root>";

    XMLParser parser;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = parser.parse(xml);
    auto end = std::chrono::high_resolution_clock::now();
    
    TEST_ASSERT(result.has_value(), "Large XML parsing should succeed");
    
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "\n  [Performance] Parsed 1000 elements in " << elapsed.count() << " ms" << std::endl;
    
    auto doc = std::move(result.value());
    auto* root = doc->getDocumentElement();
    TEST_ASSERT(root->children.size() == 1000, "Should have 1000 children");
    
    return true;
}

bool test_whitespace_handling() {
    std::string xml = "<root>  <child>  Text  </child>  </root>";
    
    {
        XMLParser parser;
        auto result = parser.parse(xml);
        TEST_ASSERT(result.has_value(), "Parsing should succeed");
        auto doc = std::move(result.value());
        auto* root = doc->getDocumentElement();
        
        TEST_ASSERT(root->children.size() == 1, "Should ignore whitespace-only text nodes");
        
        auto* child = static_cast<ElementNode*>(root->children[0]);
        auto* text = static_cast<TextNode*>(child->children[0]);
        TEST_ASSERT(text->getText() == "  Text  ", "Internal whitespace should be preserved");
    }

    return true;
}

int main() {
    std::cout << "=== LiteXML Optimized Tests ===" << std::endl;
    
    RUN_TEST(test_basic_parsing);
    RUN_TEST(test_zero_copy_attributes);
    RUN_TEST(test_entity_escaping_and_pooling);
    RUN_TEST(test_cdata_and_comments);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_file_parsing_and_ownership);
    RUN_TEST(test_whitespace_handling);
    RUN_TEST(test_memory_pool_efficiency);

    std::cout << "\n=== All Tests Passed ===" << std::endl;
    return 0;
}