cmake_minimum_required(VERSION 3.20)
project(litexml VERSION 2.0.0 LANGUAGES CXX)

# C++23标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================== 构建选项 ====================
option(BUILD_STATIC_LIB "Build static library" ON)
option(BUILD_SHARED_LIB "Build shared library" ON)
option(INSTALL_CMAKEDIR "Install CMake config files" ON)

# ==================== 构建类型配置 ====================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel CACHE STRING "" FORCE)

# ==================== 导出宏定义 ====================
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/litexml_export.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/litexml_export.h
    @ONLY
)

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
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/GL)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG")
        set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /LTCG")
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
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
        set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
        set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
        
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
        
    else()
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
        
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
        if(HAS_MARCH_NATIVE)
            add_compile_options(-march=native -mtune=native)
        endif()
        
        if(NOT APPLE)
            check_cxx_compiler_flag("-flto" HAS_LTO)
            if(HAS_LTO)
                add_compile_options(-flto)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
                set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto")
            endif()
        else()
            message(STATUS "LTO disabled on macOS (not supported for static libraries)")
        endif()
    endif()
endif()

# ==================== 公共源文件 ====================
set(LITEXML_SOURCES
    xml_parser.cpp
)

set(LITEXML_HEADERS
    dom_node.h
    entity_table.h
    xml_parser.h
)

# ==================== 构建静态库 ====================
if(BUILD_STATIC_LIB)
    add_library(litexml-static STATIC ${LITEXML_SOURCES})
    
    target_include_directories(litexml-static PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    
    target_include_directories(litexml-static INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/litexml>
    )
    
    target_compile_features(litexml-static PUBLIC cxx_std_23)
    
    target_compile_definitions(litexml-static PRIVATE
        LITEXML_STATIC
        $<$<CONFIG:Debug>:LITEXML_DEBUG>
        $<$<CONFIG:Debug>:LITEXML_ENABLE_STATS>
        $<$<CONFIG:Release>:LITEXML_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:LITEXML_RELWITHDEBINFO>
    )
    
    target_compile_definitions(litexml-static INTERFACE
        LITEXML_STATIC
    )
    
    set_target_properties(litexml-static PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        DEBUG_POSTFIX d
        OUTPUT_NAME litexml
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(litexml-static PRIVATE -fsanitize=address,undefined)
    endif()
    
    install(TARGETS litexml-static
        EXPORT litexml-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

# ==================== 构建动态库 ====================
if(BUILD_SHARED_LIB)
    add_library(litexml-shared SHARED ${LITEXML_SOURCES})
    
    target_include_directories(litexml-shared PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    
    target_include_directories(litexml-shared INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/litexml>
    )
    
    target_compile_features(litexml-shared PUBLIC cxx_std_23)
    
    target_compile_definitions(litexml-shared PRIVATE
        LITEXML_EXPORTING
        $<$<CONFIG:Debug>:LITEXML_DEBUG>
        $<$<CONFIG:Debug>:LITEXML_ENABLE_STATS>
        $<$<CONFIG:Release>:LITEXML_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:LITEXML_RELWITHDEBINFO>
    )
    
    target_compile_definitions(litexml-shared INTERFACE
        LITEXML_SHARED
    )
    
    set_target_properties(litexml-shared PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        DEBUG_POSTFIX d
        OUTPUT_NAME litexml
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(litexml-shared PRIVATE -fsanitize=address,undefined)
    endif()
    
    if(MSVC)
        set_target_properties(litexml-shared PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
        )
    endif()
    
    install(TARGETS litexml-shared
        EXPORT litexml-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

# ==================== 创建别名目标 ====================
if(BUILD_STATIC_LIB AND BUILD_SHARED_LIB)
    add_library(litexml ALIAS litexml-static)
elseif(BUILD_STATIC_LIB)
    add_library(litexml ALIAS litexml-static)
elseif(BUILD_SHARED_LIB)
    add_library(litexml ALIAS litexml-shared)
endif()

# ==================== 导出目标文件 ====================
install(EXPORT litexml-targets
    FILE litexml-targets.cmake
    NAMESPACE litexml::
    DESTINATION lib/cmake/litexml
)

# ==================== 配置 CMake 包文件 ====================
if(INSTALL_CMAKEDIR)
    include(CMakePackageConfigHelpers)
    
    # 配置 CMake config 文件
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/litexml-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/litexml-config.cmake
        INSTALL_DESTINATION lib/cmake/litexml
    )
    
    # 配置版本文件
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/litexml-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    # 安装 CMake 配置文件
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/litexml-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/litexml-config-version.cmake
        DESTINATION lib/cmake/litexml
    )
endif()

# ==================== 头文件安装 ====================
install(FILES
    ${LITEXML_HEADERS}
    ${CMAKE_CURRENT_BINARY_DIR}/litexml_export.h
    DESTINATION include/litexml
)

# ==================== 测试 ====================
enable_testing()

add_executable(litexml_test test.cpp)

if(BUILD_STATIC_LIB)
    target_link_libraries(litexml_test litexml-static)
    target_compile_definitions(litexml_test PRIVATE LITEXML_STATIC)
elseif(BUILD_SHARED_LIB)
    target_link_libraries(litexml_test litexml-shared)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(litexml_test PRIVATE -fsanitize=address,undefined)
endif()

if(NOT APPLE AND HAS_LTO)
    target_compile_options(litexml_test PRIVATE -flto)
endif()

add_test(NAME litexml_tests COMMAND litexml_test)

# ==================== 打印构建配置 ====================
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build static library: ${BUILD_STATIC_LIB}")
message(STATUS "Build shared library: ${BUILD_SHARED_LIB}")
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