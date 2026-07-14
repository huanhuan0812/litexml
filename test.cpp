#include <iostream>
#include <chrono>
#include <fstream>
#include <vector>
#include <memory>
#include <xml_parser.h> // 假设这是你使用的轻量级XML解析库的头文件

using namespace litexml;
using namespace std::chrono;

std::vector<char> load_xml(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open file: " + path);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size + 1);
    file.read(buffer.data(), size);
    buffer[size] = '\0';
    return buffer;
}

void run_benchmark(const std::string& path, XMLParser& parser, 
                   const std::string& label, int iterations) {
    auto original_data = load_xml(path);
    size_t file_size_mb = original_data.size() / (1024.0 * 1024.0);
    
    std::cout << "\n=== " << label << " ===" << std::endl;
    std::cout << "File size: " << file_size_mb << " MB" << std::endl;
    
    // 预热
    for (int i = 0; i < 5; ++i) {
        std::string xml_str(original_data.data(), original_data.size());
        auto result = parser.parse(xml_str);
        if (!result) {
            std::cerr << "Warmup failed: " << result.error().toString() << std::endl;
            return;
        }
    }
    
    // 正式测试
    volatile size_t dummy = 0;
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        std::string xml_str(original_data.data(), original_data.size());
        auto result = parser.parse(xml_str);
        if (!result) {
            std::cerr << "Parse failed at iteration " << i 
                      << ": " << result.error().toString() << std::endl;
            return;
        }
        auto* doc = result->get();
        auto* root = doc->getDocumentElement();
        if (root) {
            dummy += root->getTagName().length();
            // 访问一些属性，触发命名空间解析
            for (const auto& [name, value] : root->getAttributes()) {
                dummy += name.length() + value.length();
            }
        }
    }
    
    auto end = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(end - start).count();
    double total_mb = file_size_mb * iterations;
    double seconds = us / 1000000.0;
    
    std::cout << "Throughput: " << (total_mb / seconds) << " MB/s" << std::endl;
    std::cout << "Dummy: " << dummy << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <xml_file> [iterations]" << std::endl;
        return 1;
    }
    
    const std::string path = argv[1];
    const int iterations = (argc > 2) ? std::stoi(argv[2]) : 50;
    
    // ========== 配置1：开启命名空间解析 ==========
    XMLParser::Config config_with_ns;
    config_with_ns.preserveWhitespace = false;
    config_with_ns.parseComments = false;
    config_with_ns.parseProcessingInstructions = false;
    config_with_ns.strictMode = true;
    config_with_ns.parseNamespaces = true;
    config_with_ns.maxDepth = 1000;
    XMLParser parser_with_ns(config_with_ns);
    run_benchmark(path, parser_with_ns, "WITH Namespace Parsing", iterations);
    
    // ========== 配置2：关闭命名空间解析 ==========
    XMLParser::Config config_without_ns = config_with_ns;
    config_without_ns.parseNamespaces = false;
    XMLParser parser_without_ns(config_without_ns);
    run_benchmark(path, parser_without_ns, "WITHOUT Namespace Parsing", iterations);
    
    return 0;
}