cmake_minimum_required(VERSION 3.20)
project(litexml VERSION 2.0.0 LANGUAGES CXX)

# C++23标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================== 构建类型配置 ====================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel CACHE STRING "" FORCE)

# ==================== 编译器和平台检测 ====================
if(MSVC)
    # MSVC 配置
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /Ob2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MD /Zi /O2 /Ob1 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "/MD /O1 /Ob1 /DNDEBUG")
    
    add_compile_options(
        /W4
        /WX-
        /permissive-
        /Zc:__cplusplus
        /Zc:inline
        /Zc:strictStrings
        /volatile:iso
        /EHsc
        /std:c++23
        /MP
    )
    
    # LTO for MSVC
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/GL)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG")
        set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /LTCG")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # GCC/Clang 配置
    
    # 基础警告
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
        -Wnull-dereference
        -Wno-missing-field-initializers
    )
    
    # 根据构建类型设置优化选项
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(
            -O0
            -g
            -fno-omit-frame-pointer
            -fstack-protector-all
            -fsanitize=address
            -fsanitize=undefined
        )
        
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
        
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(
            -O2
            -g
            -fno-omit-frame-pointer
            -DNDEBUG
        )
        
    elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        add_compile_options(
            -Os
            -fno-omit-frame-pointer
            -DNDEBUG
        )
        
    else() # Release
        add_compile_options(
            -O3
            -DNDEBUG
            -fomit-frame-pointer
            -fno-rtti
            -fstrict-aliasing
            -funroll-loops
            -finline-functions
            -fvisibility=hidden
        )
        
        # 检测是否支持 -march=native
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
        if(HAS_MARCH_NATIVE)
            add_compile_options(-march=native -mtune=native)
        endif()
        
        # LTO 支持 - 仅在 GCC 和 Linux 上启用
        # macOS 上的 Apple Clang 不支持静态库的 LTO
        if(NOT APPLE)
            check_cxx_compiler_flag("-flto" HAS_LTO)
            if(HAS_LTO)
                add_compile_options(-flto)
                # 对于静态库，LTO 通过编译选项处理，不需要链接器标志
                # 但对于可执行文件，需要链接器标志
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
            endif()
        else()
            message(STATUS "LTO disabled on macOS (not supported for static libraries)")
        endif()
    endif()
endif()

# ==================== 库构建 ====================
add_library(litexml STATIC
    xml_parser.cpp
)

target_include_directories(litexml PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(litexml INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(litexml PUBLIC cxx_std_23)

set_target_properties(litexml PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    DEBUG_POSTFIX d
)

target_compile_definitions(litexml PRIVATE
    $<$<CONFIG:Debug>:LITEXML_DEBUG>
    $<$<CONFIG:Debug>:LITEXML_ENABLE_STATS>
    $<$<CONFIG:Release>:LITEXML_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:LITEXML_RELWITHDEBINFO>
)

# ==================== 测试 ====================
enable_testing()

add_executable(litexml_test test.cpp)
target_link_libraries(litexml_test litexml)

# 为测试启用 LTO（如果支持）
if(NOT APPLE AND HAS_LTO)
    target_compile_options(litexml_test PRIVATE -flto)
endif()

add_test(NAME litexml_tests COMMAND litexml_test)

# ==================== 安装 ====================
install(TARGETS litexml
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    dom_node.h
    entity_table.h
    xml_parser.h
    DESTINATION include/litexml
)

# ==================== 打印构建配置 ====================
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Optimization: O3")
    if(HAS_MARCH_NATIVE)
        message(STATUS "Architecture: native")
    endif()
    if(NOT APPLE AND HAS_LTO)
        message(STATUS "LTO: Enabled")
    else()
        message(STATUS "LTO: Disabled on macOS")
    endif()
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug: ASAN and UBSAN enabled")
endif()