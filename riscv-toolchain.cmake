set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MRS3 同时自带 GCC8（RISC-V Embedded GCC）、GCC12（RISC-V Embedded GCC12）和
# GCC15（RISC-V Embedded GCC15）。这里沿用原来的 GCC8 工具链，只更新安装路径。
set(TOOLCHAIN_BIN "D:/IDE/MounRiver_Studio3/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin")
set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN}/riscv-none-embed-gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/riscv-none-embed-g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_BIN}/riscv-none-embed-gcc.exe")

set(RISCV_ARCH_FLAGS "-march=rv32imafc -mabi=ilp32f")

set(CMAKE_C_FLAGS_INIT   "${RISCV_ARCH_FLAGS} -msmall-data-limit=8 -msave-restore -fmax-errors=20 -O3 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -std=gnu99")
set(CMAKE_CXX_FLAGS_INIT "${RISCV_ARCH_FLAGS} -msmall-data-limit=8 -msave-restore -fmax-errors=20 -O3 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "${RISCV_ARCH_FLAGS} -msmall-data-limit=8 -msave-restore -g")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)