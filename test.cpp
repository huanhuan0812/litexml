// test.cpp
#include "xml_stream.hpp"
#include <iostream>
#include <fstream>
#include <memory>
#include <iomanip>
#include <filesystem>

using namespace litexml::stream;

// ============ 测试辅助函数 ============
void print_separator(const std::string& title = "") {
    std::cout << "\n" << std::string(60, '=') << "\n";
    if (!title.empty()) {
        std::cout << "  " << title << "\n";
        std::cout << std::string(60, '=') << "\n";
    }
}

void print_location(const Location& loc) {
    std::cout << "  Location: " << loc.to_string() << "\n";
}

void print_element_info(const ElementInfo& elem, int indent = 0) {
    std::string indent_str(indent * 2, ' ');
    std::cout << indent_str << "Element: " << elem.name() << "\n";
    std::cout << indent_str << "  Local Name: " << elem.local_name() << "\n";
    std::cout << indent_str << "  Namespace URI: " << elem.namespace_uri() << "\n";
    std::cout << indent_str << "  Depth: " << elem.depth() << "\n";
    std::cout << indent_str << "  Empty: " << (elem.is_empty() ? "true" : "false") << "\n";
    
    if (!elem.attributes().empty()) {
        std::cout << indent_str << "  Attributes:\n";
        for (const auto& attr : elem.attributes()) {
            std::cout << indent_str << "    " << attr.name() << " = \"" << attr.value() << "\"\n";
            if (!attr.namespace_uri().empty()) {
                std::cout << indent_str << "      (ns: " << attr.namespace_uri() << ")\n";
            }
        }
    }
    
    if (!elem.namespaces().empty()) {
        std::cout << indent_str << "  Namespaces:\n";
        for (const auto& ns : elem.namespaces()) {
            std::cout << indent_str << "    " << ns.prefix << " = \"" << ns.uri << "\"\n";
        }
    }
}

// ============ 自定义事件处理器 ============
class TestEventHandler : public EventHandler {
public:
    void on_start_document() override {
        std::cout << "[Document] Start\n";
        indent_level_ = 0;
    }
    
    void on_end_document() override {
        std::cout << "[Document] End\n";
    }
    
    void on_start_element(const ElementInfo& element) override {
        std::string indent(indent_level_ * 2, ' ');
        std::cout << indent << "[Start] ";
        print_element_info(element, indent_level_);
        indent_level_++;
    }
    
    void on_end_element(const ElementInfo& element) override {
        indent_level_--;
        std::string indent(indent_level_ * 2, ' ');
        std::cout << indent << "[End] " << element.name() << "\n";
    }
    
    void on_characters(std::string_view text) override {
        if (!text.empty()) {
            std::string indent(indent_level_ * 2, ' ');
            std::string content(text);
            // 去除首尾空白以便显示
            content.erase(0, content.find_first_not_of(" \t\n\r"));
            content.erase(content.find_last_not_of(" \t\n\r") + 1);
            if (!content.empty()) {
                std::cout << indent << "[Text] \"" << content << "\"\n";
            }
        }
    }
    
    void on_cdata(std::string_view text) override {
        std::string indent(indent_level_ * 2, ' ');
        std::cout << indent << "[CDATA] \"" << text << "\"\n";
    }
    
    void on_comment(std::string_view text) override {
        std::cout << "[Comment] \"" << text << "\"\n";
    }
    
    void on_processing_instruction(std::string_view target, std::string_view data) override {
        std::cout << "[PI] " << target << " \"" << data << "\"\n";
    }
    
    void on_error(const ParseError& error) override {
        std::cout << "[Error] " << error.what() << "\n";
    }
    
private:
    int indent_level_ = 0;
};

// ============ 测试用例 ============

// 测试1: 基本XML解析
void test_basic_xml() {
    print_separator("测试1: 基本XML解析");
    
    std::string xml = R"(
        <?xml version="1.0" encoding="UTF-8"?>
        <book id="123" category="fiction">
            <title>The Great Gatsby</title>
            <author>F. Scott Fitzgerald</author>
            <price currency="USD">12.99</price>
            <available>true</available>
        </book>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    config.coalesce_text = true;
    config.expand_entities = true;
    
    StreamReader reader(xml, config);
    TestEventHandler handler;
    reader.set_handler(&handler);
    
    bool success = reader.parse();
    std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
}

// 测试2: 实体解析（使用 EntityTable）
void test_entity_parsing() {
    print_separator("测试2: 实体解析（使用 EntityTable）");
    
    std::string xml = R"(
        <?xml version="1.0"?>
        <entities>
            <!-- 基本实体 -->
            <example1>This is &amp; an example</example1>
            <example2>Use &lt; for less than</example2>
            <example3>Use &gt; for greater than</example3>
            <example4>Quote: &quot;Hello&quot;</example4>
            <example5>It&apos;s a beautiful day</example5>
            
            <!-- 扩展实体 -->
            <example6>Copyright: &copy; 2024</example6>
            <example7>Registered: &reg;</example7>
            <example8>Trademark: &trade;</example8>
            <example9>Non-breaking space: &nbsp;</example9>
            <example10>Euro: &euro; 100</example10>
            <example11>Alpha: &Alpha; Beta: &Beta;</example11>
            <example12>Less-than-or-equal: &le;</example12>
            <example13>Greater-than-or-equal: &ge;</example13>
            <example14>Not equal: &ne;</example14>
            <example15>Infinite: &infin;</example15>
            
            <!-- 数值实体 -->
            <example16>&#169; 2024</example16>
            <example17>&#xA9; 2024</example17>
            <example18>&#8364; 100</example18>
        </entities>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    config.coalesce_text = true;
    config.expand_entities = true;
    config.support_extended_entities = true;
    config.support_numeric_entities = true;
    config.strict_parsing = false;
    
    StreamReader reader(xml, config);
    TestEventHandler handler;
    reader.set_handler(&handler);
    
    bool success = reader.parse();
    std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
}

// 测试3: 回调方式
void test_callback_mode() {
    print_separator("测试3: 回调模式");
    
    std::string xml = R"(
        <library>
            <book id="001">
                <title>1984</title>
                <author>George Orwell</author>
            </book>
            <book id="002">
                <title>Brave New World</title>
                <author>Aldous Huxley</author>
            </book>
        </library>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    
    StreamReader reader(xml, config);
    
    // 使用回调
    reader.set_callback([](const ParseEvent& event) {
        switch (event.type()) {
            case EventType::StartElement: {
                const auto& elem = event.element();
                std::cout << "  [START] " << elem.name();
                if (auto attr = elem.find_attribute("id"); attr.has_value()) {
                    std::cout << " (id=" << attr->value() << ")";
                }
                std::cout << "\n";
                break;
            }
            case EventType::EndElement: {
                std::cout << "  [END] " << event.element().name() << "\n";
                break;
            }
            case EventType::Characters: {
                std::string text(event.text());
                text.erase(0, text.find_first_not_of(" \t\n\r"));
                text.erase(text.find_last_not_of(" \t\n\r") + 1);
                if (!text.empty()) {
                    std::cout << "    [TEXT] \"" << text << "\"\n";
                }
                break;
            }
            default:
                break;
        }
    });
    
    bool success = reader.parse();
    std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
}

// 测试4: 命名空间支持
void test_namespace_support() {
    print_separator("测试4: 命名空间支持");
    
    std::string xml = R"(
        <root xmlns="http://example.com/ns1"
              xmlns:ns2="http://example.com/ns2">
            <element>Hello</element>
            <ns2:element>World</ns2:element>
            <ns2:element xmlns:ns3="http://example.com/ns3">
                <ns3:child>Nested</ns3:child>
            </ns2:element>
        </root>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    config.namespace_aware = true;
    
    StreamReader reader(xml, config);
    TestEventHandler handler;
    reader.set_handler(&handler);
    
    bool success = reader.parse();
    std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
}

// 测试5: 自闭合元素
void test_self_closing_elements() {
    print_separator("测试5: 自闭合元素");
    
    std::string xml = R"(
        <root>
            <empty />
            <empty2 attr="value" />
            <container>
                <item name="first" />
                <item name="second" />
            </container>
        </root>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    
    StreamReader reader(xml, config);
    TestEventHandler handler;
    reader.set_handler(&handler);
    
    bool success = reader.parse();
    std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
}

// 测试6: CDATA和注释
void test_cdata_and_comments() {
    print_separator("测试6: CDATA和注释");
    
    std::string xml = R"(
        <document>
            <!-- This is a comment -->
            <content>
                <![CDATA[
                    This is CDATA content.
                    It can contain <tags> & entities &amp; without escaping.
                ]]>
            </content>
            <!-- Another comment -->
            <data>Normal text</data>
        </document>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = true;
    config.strict_parsing = false;
    
    StreamReader reader(xml, config);
    TestEventHandler handler;
    reader.set_handler(&handler);
    
    bool success = reader.parse();
    std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
}

// 测试7: 文件读取
void test_file_reading() {
    print_separator("测试7: 文件读取");
    
    // 创建测试文件
    std::string filename = "test_xml.xml";
    std::string xml_content = R"(
        <?xml version="1.0" encoding="UTF-8"?>
        <config>
            <server host="localhost" port="8080" />
            <logging level="debug">
                <file>/var/log/app.log</file>
                <rotation size="10MB" />
            </logging>
            <database>
                <driver>mysql</driver>
                <connection>
                    <host>db.example.com</host>
                    <port>3306</port>
                    <user>app_user</user>
                    <password>secure_password</password>
                </connection>
            </database>
        </config>
    )";
    
    // 写入文件
    {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << xml_content;
            file.close();
            std::cout << "Created test file: " << filename << "\n";
        } else {
            std::cout << "Failed to create test file\n";
            return;
        }
    }
    
    // 从文件读取
    try {
        ReaderConfig config;
        config.preserve_whitespace = false;
        
        auto reader = StreamReader::from_file(filename, config);
        TestEventHandler handler;
        reader->set_handler(&handler);
        
        bool success = reader->parse();
        std::cout << "\nParse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
        
        // 清理文件
        std::filesystem::remove(filename);
        std::cout << "Cleaned up test file\n";
        
    } catch (const ParseError& e) {
        std::cout << "Error: " << e.what() << "\n";
        std::filesystem::remove(filename);
    }
}

// 测试8: 流式事件处理 (next_event)
void test_streaming_events() {
    print_separator("测试8: 流式事件处理 (next_event)");
    
    std::string xml = R"(
        <root>
            <item id="1">First</item>
            <item id="2">Second</item>
            <item id="3">Third</item>
        </root>
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    config.coalesce_text = true;
    config.require_closed_tags = true;
    
    StreamReader reader(xml, config);
    
    std::cout << "Processing events one by one:\n";
    int event_count = 0;
    
    while (true) {
        auto event_opt = reader.next_event();
        if (!event_opt.has_value()) {
            break;
        }
        
        const auto& event = event_opt.value();
        event_count++;
        
        switch (event.type()) {
            case EventType::StartElement: {
                const auto& elem = event.element();
                std::cout << "  " << event_count << ". START: " << elem.name();
                if (auto attr = elem.find_attribute("id"); attr.has_value()) {
                    std::cout << " (id=" << attr->value() << ")";
                }
                std::cout << "\n";
                break;
            }
            case EventType::Characters: {
                std::string text(event.text());
                text.erase(0, text.find_first_not_of(" \t\n\r"));
                text.erase(text.find_last_not_of(" \t\n\r") + 1);
                if (!text.empty()) {
                    std::cout << "  " << event_count << ". TEXT: \"" << text << "\"\n";
                }
                break;
            }
            case EventType::EndElement: {
                std::cout << "  " << event_count << ". END: " << event.element().name() << "\n";
                break;
            }
            case EventType::StartDocument:
                std::cout << "  " << event_count << ". DOCUMENT START\n";
                break;
            case EventType::EndDocument:
                std::cout << "  " << event_count << ". DOCUMENT END\n";
                break;
            default:
                break;
        }
    }
    
    std::cout << "\nTotal events: " << event_count << "\n";
}

// 测试9: 属性值转换
void test_attribute_conversion() {
    print_separator("测试9: 属性值转换");
    
    std::string xml = R"(
        <data
            int_val="42"
            hex_val="0x2A"
            float_val="3.14159"
            bool_true="true"
            bool_yes="yes"
            bool_on="on"
            date_val="2024-01-15T14:30:00Z"
        />
    )";
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    
    StreamReader reader(xml, config);
    
    // 使用回调处理
    reader.set_callback([](const ParseEvent& event) {
        if (event.type() == EventType::StartElement) {
            const auto& elem = event.element();
            std::cout << "Element: " << elem.name() << "\n";
            
            // 整数转换
            if (auto attr = elem.find_attribute("int_val"); attr.has_value()) {
                if (auto val = attr->as_integer<int>(); val.has_value()) {
                    std::cout << "  int_val = " << val.value() << " (as int)\n";
                }
                // 布尔转换
                std::cout << "  int_val as boolean = " << (attr->as_boolean() ? "true" : "false") << "\n";
            }
            
            // 浮点数转换
            if (auto attr = elem.find_attribute("float_val"); attr.has_value()) {
                if (auto val = attr->as_float<double>(); val.has_value()) {
                    std::cout << "  float_val = " << std::setprecision(6) << val.value() << " (as double)\n";
                }
            }
            
            // 布尔转换
            if (auto attr = elem.find_attribute("bool_true"); attr.has_value()) {
                std::cout << "  bool_true = " << (attr->as_boolean() ? "true" : "false") << "\n";
            }
            if (auto attr = elem.find_attribute("bool_yes"); attr.has_value()) {
                std::cout << "  bool_yes = " << (attr->as_boolean() ? "true" : "false") << "\n";
            }
            
            // 日期转换
            if (auto attr = elem.find_attribute("date_val"); attr.has_value()) {
                if (auto tp = attr->as_datetime(); tp.has_value()) {
                    auto time = std::chrono::system_clock::to_time_t(tp.value());
                    std::cout << "  date_val = " << std::ctime(&time);
                }
            }
        }
    });
    
    reader.parse();
}

// 测试10: 错误处理
void test_error_handling() {
    print_separator("测试10: 错误处理");
    
    std::vector<std::string> test_cases = {
        // 格式错误的XML
        R"(<root><unclosed>)",
        
        // 未闭合的实体
        R"(<root>&amp</root>)",
        
        // 无效的实体
        R"(<root>&invalid_entity;</root>)",
        
        // 不匹配的标签
        R"(<root><child></child2></root>)",
        
        // 未闭合的标签
        R"(<root><child>)",
    };
    
    for (size_t i = 0; i < test_cases.size(); i++) {
        std::cout << "\n--- 测试用例 " << (i + 1) << " ---\n";
        std::cout << "Input: " << test_cases[i] << "\n";
        
        ReaderConfig config;
        config.strict_parsing = true;
        config.expand_entities = true;
        config.require_closed_tags = true;
        config.allow_incomplete_documents = false;
        
        StreamReader reader(test_cases[i], config);
        TestEventHandler handler;
        reader.set_handler(&handler);
        
        bool success = reader.parse();
        std::cout << "Result: " << (success ? "SUCCESS" : "FAILED (expected)") << "\n";
    }
}

// 测试11: 性能测试（大量数据）
void test_performance() {
    print_separator("测试11: 性能测试");
    
    // 构建一个较大的XML文档
    std::string xml = R"(<?xml version="1.0"?><root>)";
    for (int i = 0; i < 1000; i++) {
        xml += "<item id=\"" + std::to_string(i) + "\">";
        xml += "Content " + std::to_string(i);
        xml += " &amp; with entities &copy; &#169;";
        xml += "</item>";
    }
    xml += "</root>";
    
    std::cout << "XML size: " << xml.size() << " bytes\n";
    std::cout << "Items: 1000\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    ReaderConfig config;
    config.preserve_whitespace = false;
    config.expand_entities = true;
    config.support_extended_entities = true;
    config.support_numeric_entities = true;
    
    StreamReader reader(xml, config);
    
    int element_count = 0;
    reader.set_callback([&element_count](const ParseEvent& event) {
        if (event.type() == EventType::StartElement) {
            element_count++;
        }
    });
    
    bool success = reader.parse();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Parse result: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Elements parsed: " << element_count << "\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "Throughput: " << (xml.size() * 1000.0 / duration.count() / 1024.0) << " KB/s\n";
}

// ============ 主函数 ============
int main() {
    std::cout << "========================================\n";
    std::cout << "  LiteXML Stream Parser Test Suite\n";
    std::cout << "  Using EntityTable for entity support\n";
    std::cout << "========================================\n";
    
    try {
        test_basic_xml();
        test_entity_parsing();
        test_callback_mode();
        test_namespace_support();
        test_self_closing_elements();
        test_cdata_and_comments();
        test_file_reading();
        test_streaming_events();
        test_attribute_conversion();
        test_error_handling();
        test_performance();
        
        print_separator("所有测试完成");
        std::cout << "✅ All tests completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}