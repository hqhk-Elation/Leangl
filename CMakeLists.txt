cmake_minimum_required(VERSION 3.20)

project(LeanGL C)

# Output the compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# User Config Options

# 构建主机端测试（Win32 模拟器 + 分块刷新单元测试）。
# 测试程序使用主机 ABI，必须用主机编译器构建，不能用 riscv-toolchain.cmake。
option(LEANGL_BUILD_TESTS "Build host test programs (requires a host compiler)" OFF)

# CMAKE_CROSSCOMPILING 只有在 project() 之后才有确定值，所以校验放在这里，
# 尽早失败，避免用户等完整个配置流程才看到错误。
if(LEANGL_BUILD_TESTS AND CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "LEANGL_BUILD_TESTS=ON is incompatible with a toolchain file (cross build).\n"
        "The test programs need a host compiler; configure a separate build directory:\n"
        "  cmake --preset host && cmake --build --preset host && ctest --preset host")
endif()

# Include Path (INTERFACE)
add_library(leangl_headers INTERFACE)
target_include_directories(leangl_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------
# 源文件清单：整个工程只在这里维护一次。
#
# 各子目录的 OBJECT 库和 test/ 下的测试目标都从这里取文件，避免新增源文件
# 时出现“库能编过、测试漏编”的不一致。路径一律用绝对形式，因为 INTERFACE
# 目标的源文件属性是相对“定义该目标的目录”解析的，绝对路径最不容易出错。
# ------------------------------------------------------------------
set(LEANGL_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/core/leangl_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/core/leangl_dirty.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/core/leangl_flush.c
)
set(LEANGL_FONT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/font/leangl_font.c
)
set(LEANGL_DRAW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/draw/leangl_draw_char.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/draw/leangl_draw_shape.c
)
set(LEANGL_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/port/leangl_port.c
)
set(LEANGL_WIDGET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/widgets/leangl_widget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/widgets/leangl_label.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/widgets/leangl_button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/widgets/leangl_list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/widgets/leangl_scroll_label.c
)
set(LEANGL_SOURCES
    ${LEANGL_CORE_SOURCES}
    ${LEANGL_FONT_SOURCES}
    ${LEANGL_DRAW_SOURCES}
    ${LEANGL_PORT_SOURCES}
    ${LEANGL_WIDGET_SOURCES}
)

# 把整套源码当作 INTERFACE 源文件对外提供。测试目标链接它即可获得与库完全
# 相同的源文件集合，同时还能各自追加编译宏（例如 LEANGL_FLUSH_ASYNC）。
add_library(leangl_sources INTERFACE)
target_sources(leangl_sources INTERFACE ${LEANGL_SOURCES})
target_link_libraries(leangl_sources INTERFACE leangl_headers)

# Add source dir
add_subdirectory(source)

# Package
add_library(leangl STATIC 
    $<TARGET_OBJECTS:leangl_core>
    $<TARGET_OBJECTS:leangl_draw>
)
target_link_libraries(leangl PUBLIC
    leangl_core
    leangl_font
    leangl_draw
    leangl_port
    leangl_widgets
)

# Tests
if(LEANGL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
