cmake_minimum_required(VERSION 3.20)
project(litexml VERSION 2.0.0 LANGUAGES CXX)

# C++23标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用编译优化和警告
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O2 -pedantic)
endif()

# 库
add_library(litexml STATIC
    xml_parser.cpp
)

# 使用PRIVATE避免传播到依赖项
target_include_directories(litexml PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 为使用该库的目标添加头文件路径
target_include_directories(litexml INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(litexml PUBLIC cxx_std_23)

# 示例程序

# 测试
enable_testing()

add_executable(litexml_test test.cpp)
target_link_libraries(litexml_test litexml)

add_test(NAME litexml_tests COMMAND litexml_test)