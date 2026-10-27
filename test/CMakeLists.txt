# LeanGL 主机端测试。
#
# 这些程序链接 Win32 / 使用主机 ABI，不能用 RISC-V 交叉编译器构建，
# 因此只在“主机编译”的构建目录里启用（见根 CMakeLists 的 LEANGL_BUILD_TESTS）：
#
#   cmake --preset host
#   cmake --build --preset host
#   ctest --preset host --output-on-failure
#
# 被测源码由根 CMakeLists.txt 的 leangl_sources 统一提供，这里不再重复列举；
# 测试目标可以在此基础上追加自己的编译宏。
#
# 生成的可执行文件统一输出到 test/build/（见文件末尾的重定向设置），
# CTest 用目标名调用它们，因此搬动路径不影响测试注册。

set(LEANGL_TEST_WARNINGS -Wall -Wextra)

# ------------------------------------------------------------------
# 1. 分块刷新单元测试
#
# LEANGL_FLUSH_ASYNC 决定缓冲区忙标志的处理方式，会改变 core 的编译结果，
# 所以本目标必须显式定义它（库本身用的是默认的阻塞模式）。
# ------------------------------------------------------------------
add_executable(test_flush_async test_flush_async.c)
target_link_libraries(test_flush_async PRIVATE leangl_sources)
target_compile_definitions(test_flush_async PRIVATE LEANGL_FLUSH_ASYNC=1)
target_compile_options(test_flush_async PRIVATE ${LEANGL_TEST_WARNINGS})

add_test(NAME flush_async COMMAND test_flush_async)

# ------------------------------------------------------------------
# 1b. 字形缓存单元测试
#
# 统计 glyph_cb 的调用次数，验证重复测量/重复绘制不再反复取模。
# 必须显式使用阻塞刷新，测试里不模拟 DMA 完成。
# ------------------------------------------------------------------
add_executable(test_glyph_cache test_glyph_cache.c)
target_link_libraries(test_glyph_cache PRIVATE leangl_sources)
target_compile_definitions(test_glyph_cache PRIVATE LEANGL_FLUSH_ASYNC=0)
target_compile_options(test_glyph_cache PRIVATE ${LEANGL_TEST_WARNINGS})

add_test(NAME glyph_cache COMMAND test_glyph_cache)

# ------------------------------------------------------------------
# 2. Win32 屏幕模拟器
#
# 用真实窗口模拟 LCD，走库默认的阻塞式刷新路径。
#
# 引擎（leangl_win32_sim.c）与界面（leangl_sim_demo.c）是分开的：
# 换界面只需替换 leangl_sim_demo.c，见 leangl_sim.h 的说明。
#
# unifont.c 提供从 GNU Unifont 的 .hex 文件加载字库的能力；找不到文件时
# 模拟器会回退到 GDI 现场渲染，因此不依赖外部数据也能跑。
# ------------------------------------------------------------------
if(WIN32)
    set(LEANGL_SIM_SOURCES
        leangl_win32_sim.c
        leangl_sim_demo.c
        unifont.c
    )

    # 2a. 阻塞式
    add_executable(leangl_win32_sim ${LEANGL_SIM_SOURCES})
    target_link_libraries(leangl_win32_sim PRIVATE leangl_sources gdi32)
    target_compile_options(leangl_win32_sim PRIVATE ${LEANGL_TEST_WARNINGS})

    # 2b. 异步式
    #
    # LEANGL_FLUSH_ASYNC 是编译期宏，会改变 buffer / flush 的实现，所以异步
    # 版本必须是独立目标，不能靠运行期开关切换。
    add_executable(leangl_win32_sim_async ${LEANGL_SIM_SOURCES})
    target_link_libraries(leangl_win32_sim_async PRIVATE leangl_sources gdi32)
    target_compile_definitions(leangl_win32_sim_async PRIVATE LEANGL_FLUSH_ASYNC=1)
    target_compile_options(leangl_win32_sim_async PRIVATE ${LEANGL_TEST_WARNINGS})

    # 自检模式不需要窗口，可在 CI 里跑。
    # 退出码非 0 表示有像素带没刷到（分块刷新饥饿），正好守住 P0 回归。
    #
    # WORKING_DIRECTORY 指向源码目录，让模拟器能按默认路径找到
    # unifont.hex；找不到时它会回退到 GDI 渲染，测试仍然有效。
    add_test(NAME win32_sim_selftest
             COMMAND leangl_win32_sim --selftest 30
                     --dump ${CMAKE_CURRENT_BINARY_DIR}/sim_blocking.bmp)
    add_test(NAME win32_sim_async_selftest
             COMMAND leangl_win32_sim_async --selftest 60
                     --dump ${CMAKE_CURRENT_BINARY_DIR}/sim_async.bmp)
    set_tests_properties(win32_sim_selftest win32_sim_async_selftest PROPERTIES
                         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # ------------------------------------------------------------------
    # 2c.【临时对照】用修复前的 LEANGL_Flush() 构建同一套界面
    #
    # leangl_flush_legacy.c 与 source/core/leangl_flush.c 定义同名符号，所以
    # 必须把后者从源文件列表里剔除，再用前者顶上。LEANGL_SOURCES 是根
    # CMakeLists.txt 定义的变量，子目录可以直接继承使用。
    #
    # 异步模式下运行即可看到修复前的饥饿现象：屏幕上方几条像素带反复重绘，
    # 下方永远不更新，自检会报"有像素带缺失"并以非 0 退出。
    #
    # 用途仅限对比，因此不注册进 CTest（避免 ctest 常红）。对照完成后
    # 删除本目标与 leangl_flush_legacy.c 即可。
    # ------------------------------------------------------------------
    option(LEANGL_BUILD_LEGACY_COMPARE "Build the pre-fix flush variant for comparison" ON)

    if(LEANGL_BUILD_LEGACY_COMPARE)
        set(_leangl_legacy_sources ${LEANGL_SOURCES})
        list(REMOVE_ITEM _leangl_legacy_sources
            ${CMAKE_SOURCE_DIR}/source/core/leangl_flush.c)

        add_executable(leangl_win32_sim_legacy
            ${LEANGL_SIM_SOURCES}
            leangl_flush_legacy.c
            ${_leangl_legacy_sources}
        )
        target_link_libraries(leangl_win32_sim_legacy PRIVATE leangl_headers gdi32)
        target_compile_definitions(leangl_win32_sim_legacy PRIVATE LEANGL_FLUSH_ASYNC=1)
        target_compile_options(leangl_win32_sim_legacy PRIVATE ${LEANGL_TEST_WARNINGS})
    endif()
endif()

# ------------------------------------------------------------------
# 3. 输出目录：所有测试程序统一放到 test/build/，且不产生配置名子目录。
#
# 为什么不用全局 CMAKE_RUNTIME_OUTPUT_DIRECTORY：那会影响所有目录的 exe，
# 且无法把“测试产物”和真正的产品输出区分开。限定在本目录，作用范围明确。
#
# 多配置生成器（Visual Studio / Xcode）会在每个 target 的默认输出路径后面
# 追加配置名，所以 RUNTIME_OUTPUT_DIRECTORY_<CONFIG> 必须逐个设成同一路径，
# 否则会出现 test/build/Debug/xxx.exe。
#
# PDB 也重定向到构建树：MSVC 会把 .pdb 写在可执行文件旁边，那样 test/build
# 里就不止有 exe 了。MinGW/GCC 不产生 .pdb，这两项对它是无害的空操作。
#
# 本节必须放在文件末尾——BUILDSYSTEM_TARGETS 只反映查询那一刻已定义的目标。
# ------------------------------------------------------------------
set(LEANGL_TEST_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build")

function(leangl_set_test_output target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY                "${LEANGL_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${LEANGL_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${LEANGL_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${LEANGL_TEST_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${LEANGL_TEST_OUTPUT_DIR}"
        PDB_OUTPUT_DIRECTORY                    "${CMAKE_CURRENT_BINARY_DIR}/pdb"
        COMPILE_PDB_OUTPUT_DIRECTORY            "${CMAKE_CURRENT_BINARY_DIR}/pdb"
    )
endfunction()

# 遍历本目录下所有已定义目标，只处理可执行文件。用遍历而不是逐个手写，
# 将来新增测试程序时不会漏掉重定向。
get_property(_leangl_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_leangl_target IN LISTS _leangl_test_targets)
    get_target_property(_leangl_type ${_leangl_target} TYPE)
    if(_leangl_type STREQUAL "EXECUTABLE")
        leangl_set_test_output(${_leangl_target})
    endif()
endforeach()
