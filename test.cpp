// test_entity.cpp - 修复空格测试，直接使用字节比较
#include "entity_table.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

using namespace litexml;

struct TestResult {
    std::string name;
    bool passed;
    std::string input;
    std::string expected;
    std::string actual;
};

std::vector<TestResult> g_results;

// 显示字符串的十六进制表示
std::string hexString(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if (result.empty()) {
            result += "0x";
        } else {
            result += " 0x";
        }
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", c);
        result += buf;
    }
    if (result.empty()) result = "(empty)";
    return result;
}

std::string debugString(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if (c == ' ') {
            result += "[SPACE]";
        } else if (c == '\t') {
            result += "[TAB]";
        } else if (c == '\n') {
            result += "[LF]";
        } else if (c == '\r') {
            result += "[CR]";
        } else if (c == 0xA0) {
            result += "[NBSP]";
        } else if (c == 0xE2) {
            // 检查后面的字节
            if (s.size() > 2) {
                unsigned char c2 = s[1];
                unsigned char c3 = s[2];
                if (c2 == 0x80 && c3 == 0x82) {
                    result += "[ENSP]";
                } else if (c2 == 0x80 && c3 == 0x83) {
                    result += "[EMSP]";
                } else if (c2 == 0x80 && c3 == 0xA6) {
                    result += "[HELLIP]";
                } else if (c2 == 0x82 && c3 == 0xAC) {
                    result += "[EURO]";
                } else {
                    result += "[UTF8]";
                }
            } else {
                result += "[UTF8]";
            }
        } else if (c < 32) {
            result += "[0x" + std::to_string(c) + "]";
        } else if (c >= 0x80 && c < 0xC0) {
            // UTF-8 连续字节，跳过
            continue;
        } else {
            result += static_cast<char>(c);
        }
    }
    return result;
}

void runTest(const std::string& name, const std::string& input, const std::string& expected) {
    std::string actual = EntityTable::decode_entities(input);
    bool passed = (actual == expected);
    g_results.push_back({name, passed, input, expected, actual});
    
    if (!passed) {
        std::cout << "❌ " << name << "\n";
        std::cout << "  输入: " << debugString(input) << "\n";
        std::cout << "  期望: " << debugString(expected) << " (hex: " << hexString(expected) << ")\n";
        std::cout << "  实际: " << debugString(actual) << " (hex: " << hexString(actual) << ")\n\n";
    }
}

void testBasicEntities() {
    runTest("&amp;", "&amp;", "&");
    runTest("&lt;", "&lt;", "<");
    runTest("&gt;", "&gt;", ">");
    runTest("&quot;", "&quot;", "\"");
    runTest("&apos;", "&apos;", "'");
    runTest("混合文本", "Hello &amp; World", "Hello & World");
    runTest("标签包围", "&lt;tag&gt;", "<tag>");
}

void testExtendedEntities() {
    runTest("&copy;", "&copy;", "©");
    runTest("&reg;", "&reg;", "®");
    runTest("&deg;", "&deg;", "°");
    runTest("&plusmn;", "&plusmn;", "±");
    runTest("&micro;", "&micro;", "µ");
    runTest("&frac14;", "&frac14;", "¼");
    runTest("&frac12;", "&frac12;", "½");
    runTest("&frac34;", "&frac34;", "¾");
    
    runTest("&Agrave;", "&Agrave;", "À");
    runTest("&Aacute;", "&Aacute;", "Á");
    runTest("&Auml;", "&Auml;", "Ä");
    runTest("&agrave;", "&agrave;", "à");
    runTest("&aacute;", "&aacute;", "á");
    runTest("&auml;", "&auml;", "ä");
    runTest("&szlig;", "&szlig;", "ß");
    runTest("&ETH;", "&ETH;", "Ð");
    runTest("&eth;", "&eth;", "ð");
}

void testGreekEntities() {
    runTest("&Alpha;", "&Alpha;", "Α");
    runTest("&Beta;", "&Beta;", "Β");
    runTest("&Gamma;", "&Gamma;", "Γ");
    runTest("&Delta;", "&Delta;", "Δ");
    runTest("&Theta;", "&Theta;", "Θ");
    runTest("&Lambda;", "&Lambda;", "Λ");
    runTest("&Pi;", "&Pi;", "Π");
    runTest("&Sigma;", "&Sigma;", "Σ");
    runTest("&Phi;", "&Phi;", "Φ");
    runTest("&Psi;", "&Psi;", "Ψ");
    runTest("&Omega;", "&Omega;", "Ω");
    runTest("&alpha;", "&alpha;", "α");
    runTest("&beta;", "&beta;", "β");
    runTest("&gamma;", "&gamma;", "γ");
    runTest("&delta;", "&delta;", "δ");
    runTest("&theta;", "&theta;", "θ");
    runTest("&lambda;", "&lambda;", "λ");
    runTest("&mu;", "&mu;", "μ");
    runTest("&pi;", "&pi;", "π");
    runTest("&sigma;", "&sigma;", "σ");
    runTest("&phi;", "&phi;", "φ");
    runTest("&omega;", "&omega;", "ω");
}

void testMathEntities() {
    runTest("&forall;", "&forall;", "∀");
    runTest("&part;", "&part;", "∂");
    runTest("&exist;", "&exist;", "∃");
    runTest("&empty;", "&empty;", "∅");
    runTest("&nabla;", "&nabla;", "∇");
    runTest("&isin;", "&isin;", "∈");
    runTest("&notin;", "&notin;", "∉");
    runTest("&prod;", "&prod;", "∏");
    runTest("&sum;", "&sum;", "∑");
    runTest("&minus;", "&minus;", "−");
    runTest("&radic;", "&radic;", "√");
    runTest("&prop;", "&prop;", "∝");
    runTest("&infin;", "&infin;", "∞");
    runTest("&ang;", "&ang;", "∠");
    runTest("&and;", "&and;", "∧");
    runTest("&or;", "&or;", "∨");
    runTest("&cap;", "&cap;", "∩");
    runTest("&cup;", "&cup;", "∪");
    runTest("&int;", "&int;", "∫");
    runTest("&there4;", "&there4;", "∴");
    runTest("&sim;", "&sim;", "∼");
    runTest("&cong;", "&cong;", "≅");
    runTest("&asymp;", "&asymp;", "≈");
    runTest("&ne;", "&ne;", "≠");
    runTest("&equiv;", "&equiv;", "≡");
    runTest("&le;", "&le;", "≤");
    runTest("&ge;", "&ge;", "≥");
    runTest("&sub;", "&sub;", "⊂");
    runTest("&sup;", "&sup;", "⊃");
    runTest("&sube;", "&sube;", "⊆");
    runTest("&supe;", "&supe;", "⊇");
    runTest("&oplus;", "&oplus;", "⊕");
    runTest("&otimes;", "&otimes;", "⊗");
    runTest("&perp;", "&perp;", "⊥");
    runTest("&sdot;", "&sdot;", "⋅");
}

void testNumericEntities() {
    runTest("十进制 #160", "&#160;", std::string("\xC2\xA0", 2));
    runTest("十进制 #169", "&#169;", "©");
    runTest("十进制 #174", "&#174;", "®");
    runTest("十进制 #8364", "&#8364;", "€");
    runTest("十进制 #8734", "&#8734;", "∞");
    runTest("十六进制 #xA0", "&#xA0;", std::string("\xC2\xA0", 2));
    runTest("十六进制 #xA9", "&#xA9;", "©");
    runTest("十六进制 #xAE", "&#xAE;", "®");
    runTest("十六进制 #x20AC", "&#x20AC;", "€");
    runTest("十六进制 #x221E", "&#x221E;", "∞");
}

void testMixedEntities() {
    runTest("混合1", "Copyright &copy; 2024", "Copyright © 2024");
    runTest("混合2", "&pi; &asymp; 3.14159", "π ≈ 3.14159");
    runTest("混合3", "&lt;html&gt; &amp; &quot;content&quot;", "<html> & \"content\"");
    runTest("混合希腊", "&Alpha; &amp; &beta;", "Α & β");
    runTest("箭头", "&larr; &rarr; &harr;", "← → ↔");
    runTest("分数", "&frac14; + &frac12; = &frac34;", "¼ + ½ = ¾");
    runTest("符号", "&bull; &hellip; &euro;", "• … €");
    
    // 空格测试 - 使用 UTF-8 字符串
    std::string expected_spaces;
    expected_spaces += std::string("\xC2\xA0", 2);  // &nbsp; 不断行空格
    expected_spaces += " ";                          // 普通空格
    expected_spaces += "\xE2\x80\x82";              // &ensp; en space
    expected_spaces += " ";                          // 普通空格
    expected_spaces += "\xE2\x80\x83";              // &emsp; em space
    
    runTest("空格", "&nbsp; &ensp; &emsp;", expected_spaces);
}

void testUnknownEntities() {
    runTest("未知实体", "&unknown;", "&unknown;");
    runTest("不完整", "incomplete &amp", "incomplete &amp");
    runTest("不完整2", "incomplete &am", "incomplete &am");
}

void testOOXMLSpecific() {
    runTest("OOXML &", "&amp;", "&");
    runTest("OOXML <w:p>", "&lt;w:p&gt;", "<w:p>");
    runTest("OOXML rId", "&quot;rId&quot;", "\"rId\"");
    runTest("OOXML 版权", "&copy; 2024", "© 2024");
    runTest("OOXML nbsp", "&#160;", std::string("\xC2\xA0", 2));
    runTest("OOXML hellip", "&hellip;", "…");
}

void testHasEntity() {
    struct HasEntityTest {
        std::string input;
        bool expected;
        std::string name;
    };
    
    std::vector<HasEntityTest> tests = {
        {"Hello World", false, "无实体"},
        {"Hello &amp; World", true, "有实体"},
        {"&copy;", true, "扩展实体"},
        {"&#160;", true, "数值实体"},
        {"No entity here", false, "无实体2"},
        {"&", false, "只有 &"},
        {"&amp", false, "不完整的 &amp"},
        {"&;", false, "空的实体"},
        {"&#x;", false, "不完整的数值实体"},
    };
    
    for (const auto& test : tests) {
        bool result = EntityTable::has_entity(test.input);
        if (result != test.expected) {
            std::cout << "❌ has_entity: " << test.name << "\n";
            std::cout << "  输入: " << debugString(test.input) << "\n";
            std::cout << "  期望: " << (test.expected ? "true" : "false") << "\n";
            std::cout << "  实际: " << (result ? "true" : "false") << "\n\n";
        }
    }
}

void testPerformance() {
    std::string large_text;
    large_text.reserve(60000);
    for (int i = 0; i < 1000; ++i) {
        large_text += "Hello &amp; World &copy; 2024 &lt;tag&gt; &pi; &asymp; 3.14 ";
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    std::string result = EntityTable::decode_entities(large_text);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "⚡ 性能: " << large_text.size() << " 字节, " 
              << duration.count() << " ms, "
              << "输出 " << result.size() << " 字节\n\n";
}

int main() {
    std::cout << "=== LiteXML 实体解码测试 ===\n\n";
    
    testBasicEntities();
    testExtendedEntities();
    testGreekEntities();
    testMathEntities();
    testNumericEntities();
    testMixedEntities();
    testUnknownEntities();
    testOOXMLSpecific();
    testHasEntity();
    testPerformance();
    
    int total = g_results.size();
    int passed = 0;
    for (const auto& r : g_results) {
        if (r.passed) passed++;
    }
    int failed = total - passed;
    
    std::cout << "=== 结果 ===\n";
    std::cout << "总计: " << total << " 测试\n";
    std::cout << "✅ 通过: " << passed << "\n";
    std::cout << "❌ 失败: " << failed << "\n";
    
    if (failed == 0) {
        std::cout << "🎉 全部通过!\n";
    } else {
        // 显示失败测试的详细信息
        std::cout << "\n失败测试详情:\n";
        for (const auto& r : g_results) {
            if (!r.passed) {
                std::cout << "  - " << r.name << "\n";
                std::cout << "    期望: " << hexString(r.expected) << "\n";
                std::cout << "    实际: " << hexString(r.actual) << "\n";
            }
        }
    }
    
    return failed > 0 ? 1 : 0;
}