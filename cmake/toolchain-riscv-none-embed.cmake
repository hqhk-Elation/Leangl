# =====================================================================
# RISC-V 交叉编译工具链（WCH riscv-none-embed-gcc）
# 由于我只有一块WCH的ch32v317的开发板，因此使用其工具链作为示例。
#
# 只作用于库本体。主机端测试用默认编译器构建，见 CMakePresets.json 的 host 预设。
#
# 用法：
#   cmake -B build/riscv -G "MinGW Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv-none-embed.cmake
#   或者直接用预设：cmake --preset riscv
#
# 路径覆盖（按优先级）：
#   1. -DLEANGL_RISCV_TOOLCHAIN_DIR=<...>/bin
#   2. 环境变量 RISCV_TOOLCHAIN_DIR
#   3. 下面的默认值（MounRiver Studio 3 自带 GCC8）
#
# 目标三元组也可调整：
#   -DLEANGL_RISCV_TOOLCHAIN_PREFIX=riscv-none-elf-
# =====================================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# 交叉编译时不真正链接可执行文件，用静态库试探编译器即可。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---------------------------------------------------------------------
# 工具链位置
# ---------------------------------------------------------------------
set(_leangl_default_toolchain_dir
    "D:/IDE/MounRiver_Studio3/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin")

if(NOT LEANGL_RISCV_TOOLCHAIN_DIR)
    if(DEFINED ENV{RISCV_TOOLCHAIN_DIR} AND NOT "$ENV{RISCV_TOOLCHAIN_DIR}" STREQUAL "")
        set(LEANGL_RISCV_TOOLCHAIN_DIR "$ENV{RISCV_TOOLCHAIN_DIR}")
    else()
        set(LEANGL_RISCV_TOOLCHAIN_DIR "${_leangl_default_toolchain_dir}")
    endif()
endif()
set(LEANGL_RISCV_TOOLCHAIN_DIR "${LEANGL_RISCV_TOOLCHAIN_DIR}" CACHE PATH
    "RISC-V 交叉工具链的 bin 目录")

set(LEANGL_RISCV_TOOLCHAIN_PREFIX "riscv-none-embed-" CACHE STRING
    "交叉工具链的可执行文件前缀（例如 riscv-none-elf-）")

# 主机是 Windows 时带 .exe 后缀；在 Linux 下用同一套工具链也不至于找不到 gcc。
if(CMAKE_HOST_WIN32)
    set(_leangl_toolchain_suffix ".exe")
else()
    set(_leangl_toolchain_suffix "")
endif()

set(CMAKE_C_COMPILER
    "${LEANGL_RISCV_TOOLCHAIN_DIR}/${LEANGL_RISCV_TOOLCHAIN_PREFIX}gcc${_leangl_toolchain_suffix}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")

# 归档 / 反汇编工具：不指定时 CMake 会按前缀自行推导，这里显式给出更稳。
foreach(_tool AR RANLIB OBJCOPY OBJDUMP SIZE)
    string(TOLOWER "${_tool}" _tool_lc)
    set(CMAKE_${_tool}
        "${LEANGL_RISCV_TOOLCHAIN_DIR}/${LEANGL_RISCV_TOOLCHAIN_PREFIX}${_tool_lc}${_leangl_toolchain_suffix}")
endforeach()

if(NOT EXISTS "${CMAKE_C_COMPILER}")
    message(FATAL_ERROR
        "找不到 RISC-V 交叉编译器：${CMAKE_C_COMPILER}\n"
        "请用 -DLEANGL_RISCV_TOOLCHAIN_DIR=<工具链 bin 目录> 指定，"
        "或设置环境变量 RISCV_TOOLCHAIN_DIR。")
endif()

# ---------------------------------------------------------------------
# 目标架构与公共编译选项
#
# 注意：这里**不写 -O 与 -g**。优化级别和调试信息交给 CMAKE_BUILD_TYPE，
#      否则 Debug 构建也会被强制成 -O3，配置名与实际行为不符。
# ---------------------------------------------------------------------
set(LEANGL_RISCV_ARCH "-march=rv32imafc" CACHE STRING "目标架构")
set(LEANGL_RISCV_ABI "-mabi=ilp32f" CACHE STRING "目标 ABI")

set(_leangl_riscv_common_flags
    "${LEANGL_RISCV_ARCH} ${LEANGL_RISCV_ABI}"
    -msmall-data-limit=8
    -msave-restore
    -fmax-errors=20
    -fmessage-length=0
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fsingle-precision-constant)

string(JOIN " " _leangl_riscv_common_flags_str ${_leangl_riscv_common_flags})

set(CMAKE_C_FLAGS_INIT   "${_leangl_riscv_common_flags_str}")
set(CMAKE_ASM_FLAGS_INIT "${LEANGL_RISCV_ARCH} -msmall-data-limit=8 -msave-restore")

# ---------------------------------------------------------------------
# 查找规则：程序用主机工具，库/头文件只在目标根下找。
# ---------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)