# =====================================================================
# LeanGL —— MCU TFT 显示屏的小型缓冲式 GUI 库
#
# 快速上手：
#
#   主机（含 Win32 模拟器与测试，默认）：
#       cmake --preset host
#       cmake --build --preset host
#       ctest --preset host
#
#   RISC-V 交叉编译（只产出静态库）：
#       cmake --preset riscv
#       cmake --build --preset riscv
#
#   作为子项目集成（add_subdirectory / FetchContent）：
#       add_subdirectory(LeanGL)
#       target_link_libraries(app PRIVATE LeanGL::leanGL)
#
# 测试需要主机 ABI，不能与交叉工具链共用构建目录，因此分两个 preset。
# =====================================================================

cmake_minimum_required(VERSION 3.20)

project(LeanGL
    VERSION 1.0.0
    DESCRIPTION "LeanGL: a small buffered GUI library for MCU TFT displays"
    LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------
# 选项
# ---------------------------------------------------------------------

# 判断是否由本仓库顶层构建。作为子项目引入时不改变父工程的全局设置。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(LEANGL_IS_TOP_LEVEL ON)
else()
    set(LEANGL_IS_TOP_LEVEL OFF)
endif()

if(LEANGL_IS_TOP_LEVEL AND WIN32)
    set(_leangl_tests_default ON)
else()
    set(_leangl_tests_default OFF)
endif()

option(LEANGL_BUILD_TESTS
    "构建主机端测试（Win32 屏幕模拟器 + 单元测试），需要主机编译器"
    ${_leangl_tests_default})

option(LEANGL_INSTALL
    "生成 install 与 CMake 包导出规则"
    ${LEANGL_IS_TOP_LEVEL})

option(LEANGL_WARNINGS
    "为库与测试打开 -Wall -Wextra"
    ON)

set(LEANGL_FLUSH_ASYNC "" CACHE STRING
    "覆盖 leanGL_conf.h 中的 LEANGL_FLUSH_ASYNC：0=阻塞式，1=异步 DMA；留空表示由头文件决定")
set_property(CACHE LEANGL_FLUSH_ASYNC PROPERTY STRINGS "" 0 1)

# 测试程序用主机 ABI，交叉编译目录里构建必然失败。尽早拦下来，
# 避免用户等完整个配置流程才看到一屏链接错误。
if(LEANGL_BUILD_TESTS AND CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "LEANGL_BUILD_TESTS=ON 不能与工具链文件同时使用：测试需要主机编译器。\n"
        "请分开配置：\n"
        "  cmake --preset host  && cmake --build --preset host  && ctest --preset host\n"
        "  cmake --preset riscv && cmake --build --preset riscv")
endif()

# 交叉编译时不安装，避免把目标平台的 .a 混进宿主机前缀。
if(CMAKE_CROSSCOMPILING)
    set(LEANGL_INSTALL OFF CACHE BOOL "生成 install 与 CMake 包导出规则" FORCE)
endif()

# ---------------------------------------------------------------------
# 输出目录（仅顶层构建时生效，避免影响父工程）
# ---------------------------------------------------------------------
if(LEANGL_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------
# 源文件清单
#
# 全工程只在这里维护一次：库目标、测试变体、模拟器都从这里取文件，
# 新增源文件时不会出现“库能编过、测试漏编”的不一致。
# ---------------------------------------------------------------------
set(LEANGL_CORE_SOURCES
    source/core/leangl_buffer.c
    source/core/leangl_dirty.c
    source/core/leangl_flush.c
)
set(LEANGL_FONT_SOURCES
    source/font/leangl_font.c
)
set(LEANGL_DRAW_SOURCES
    source/draw/leangl_draw_char.c
    source/draw/leangl_draw_shape.c
)
set(LEANGL_PORT_SOURCES
    source/port/leangl_port.c
)
set(LEANGL_WIDGET_SOURCES
    source/widgets/leangl_widget.c
    source/widgets/leangl_label.c
    source/widgets/leangl_button.c
    source/widgets/leangl_list.c
    source/widgets/leangl_scroll_label.c
)

set(LEANGL_SOURCES
    ${LEANGL_CORE_SOURCES}
    ${LEANGL_FONT_SOURCES}
    ${LEANGL_DRAW_SOURCES}
    ${LEANGL_PORT_SOURCES}
    ${LEANGL_WIDGET_SOURCES}
)

# 转成绝对路径：子目录再使用 LEANGL_SOURCES 时不会因相对基准不同而解析错。
list(TRANSFORM LEANGL_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set(LEANGL_PUBLIC_HEADERS
    leanGL.h
    leanGL_conf.h
    include/leangl_port.h
    include/leangl_types.h
    include/leangl_font.h
    include/leangl_buffer.h
    include/leangl_dirty.h
    include/leangl_flush.h
    include/leangl_widget.h
    include/leangl_label.h
    include/leangl_button.h
    include/leangl_list.h
    include/leangl_scroll_label.h
    include/leangl_draw_char.h
    include/leangl_draw_shape.h
    include/leangl_draw_pixel.h
    include/leangl_colors.h
    include/leangl_internal.h
)

# ---------------------------------------------------------------------
# 公共编译设置
#
# 库与所有测试共用同一套 C 标准与告警策略。用函数而不是 INTERFACE 目标，
# 是为了不把内部目标带进 install(EXPORT) 的依赖图。
# ---------------------------------------------------------------------
function(leanGL_apply_build_settings target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON)

    if(LEANGL_WARNINGS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---------------------------------------------------------------------
# 头文件目标
#
# 构建树里同时需要仓库根（leanGL_conf.h、leanGL.h）与 include/；
# 安装后两者都被摊平到同一个 include 目录下。
# ---------------------------------------------------------------------
add_library(leanGL_headers INTERFACE)
add_library(LeanGL::headers ALIAS leanGL_headers)
target_include_directories(leanGL_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

# ---------------------------------------------------------------------
# 库
# ---------------------------------------------------------------------
add_library(leanGL STATIC ${LEANGL_SOURCES})
add_library(LeanGL::leanGL ALIAS leanGL)
target_link_libraries(leanGL PUBLIC leanGL_headers)
target_sources(leanGL PRIVATE ${LEANGL_PUBLIC_HEADERS})

if(NOT LEANGL_FLUSH_ASYNC STREQUAL "")
    # PUBLIC：该宏会改变缓冲区/刷新接口的语义，使用方必须看到同样的值。
    target_compile_definitions(leanGL PUBLIC LEANGL_FLUSH_ASYNC=${LEANGL_FLUSH_ASYNC})
endif()

leanGL_apply_build_settings(leanGL)

# ---------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------
if(LEANGL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------
# 安装与包导出
# ---------------------------------------------------------------------
if(LEANGL_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS leanGL leanGL_headers
        EXPORT LeanGLTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # 头文件摊平安装到 <prefix>/include，与 target_include_directories 的
    # INSTALL_INTERFACE 对应。
    install(FILES leanGL.h leanGL_conf.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT LeanGLTargets
        FILE LeanGLTargets.cmake
        NAMESPACE LeanGL::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeanGL)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LeanGLConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/LeanGLConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeanGL)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/LeanGLConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/LeanGLConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/LeanGLConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeanGL)
endif()
