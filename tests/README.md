# LeanGL 测试说明

本目录存放 LeanGL 的全部自动化测试。测试只在**宿主（host）**上运行：用假的 port 层
和一个 Win32 屏幕模拟器代替真实 MCU 与 TFT，因此不需要硬件即可验证分块刷新、
字形缓存、配置解析等核心逻辑。

- 构建系统：CMake，preset 为 `host`
- 测试驱动：CTest
- 用例总数：**9**（Win32 平台；非 Windows 平台只注册前 3 个纯逻辑用例）

## 目录结构

```
tests/
├── CMakeLists.txt                # 测试目标与 add_test 注册
├── README.md                     # 本文件
├── OFL-1.1.txt                   # SIL Open Font License 1.1 全文（字库资产用）
├── assets/
│   └── unifont_all-18.0.01.hex   # GNU Unifont 18.0.01 全字库（约 8 MB，仅测试用）
├── sim/                          # Win32 屏幕模拟器
│   ├── sim.h                     # 引擎对外接口（界面描述 / 回调约定）
│   ├── sim_config.h
│   ├── sim_config.c              # 极简 INI 解析，无第三方依赖
│   ├── sim_config.ini            # 默认工况：240x320，SPI Flash + SPI 屏延迟
│   ├── sim_config_slow.ini       # 慢速工况：320x240，每条带 2.4 ms
│   ├── sim_demo.c                # 演示界面：实现 leangl_sim_ui()
│   ├── sim_unifont.h
│   ├── sim_unifont.c             # .hex 字库加载器（主机端，提供 glyph_cb）
│   └── sim_win32.c               # 窗口、显存 DIB、刷新/DMA 调度、耗时模型
└── unit/                         # 不依赖平台 API 的单元测试
    ├── test_flush_async.c        # 分块刷新（异步 DMA 饥饿、重复发送、末块行数）
    ├── test_glyph_cache.c        # 字形缓存命中与失效
    └── test_sim_config.c         # 配置文件解析
```

`tests/.clangd`、`test/*.bmp` 等本地文件被仓库根 `.gitignore` 忽略，不属于测试内容。

## 运行测试

在仓库根目录执行：

```powershell
cmake --preset host
cmake --build --preset host
ctest --preset host
```

常用变体：

| 目的 | 命令 |
| --- | --- |
| 列出全部用例 | `ctest --preset host -N` |
| 只跑指定用例 | `ctest --preset host -R flush_async --output-on-failure` |
| 失败时打印完整输出 | `ctest --preset host --output-on-failure` |
| 并行执行 | `ctest --preset host -j` |

## 用例清单

| # | 用例名 | 被测目标 | 内容 |
| :-- | :--- | :--- | :--- |
| 1 | `flush_async` | `test_flush_async` | 异步（DMA）“发送中”永不空闲时整屏仍能刷完（饥饿回归）；已提交块不重复绘制/发送；末块不足 `buffer.lines` 时行数正确；脏区清空后不再调 `flush_cb` |
| 2 | `glyph_cache` | `test_glyph_cache` | 同一文本二次测量宽度不再增加 `glyph_cb` 调用；分块绘制与重复绘制全部命中缓存；不存在的字符只查一次（负缓存）；重新注册字体后缓存失效 |
| 3 | `sim_config` | `test_sim_config` | 默认值合理；含 BOM、CRLF、行尾注释的完整配置可正确读出；缺失键保留默认值；越界值与未知键被忽略并计数；文件不存在返回 0；`font.path` 等相对路径按配置文件所在目录解析 |
| 4 | `sim_help` | `leanGL_sim` | `--help` 可用 |
| 5 | `sim_blocking_selftest` | `leanGL_sim` | 阻塞模式下无窗口自检 5000 ms，结束后把画面存为 `sim_blocking.bmp` |
| 6 | `sim_async_selftest` | `leanGL_sim_async` | 异步模式同上，输出 `sim_async.bmp` |
| 7 | `sim_slow_mcu_blocking` | `leanGL_sim` | 慢速工况（320x240，每条带 2.4 ms、取字 300 µs）下的阻塞刷新 |
| 8 | `sim_slow_mcu_async` | `leanGL_sim_async` | 慢速工况下的异步刷新——“屏还没刷完脏区又来了”的饥饿场景 |
| 9 | `sim_no_delay` | `leanGL_sim_async` | `--no-delay` 把所有模拟延迟清零，3000 ms 内跑完，用于快速回归 |

自检（`--selftest`）的判定标准：不开窗口跑到画面稳定，**所有像素带都已被送到屏幕**，
且没有非法内存访问；任一条件不满足即以非 0 退出。用例 5–9 的 `WORKING_DIRECTORY`
被设为 `tests/`，因此配置里的相对路径按 `tests/` 解析。

## 模拟器命令行

```text
leanGL_sim[_async].exe [--config <file.ini>] [--selftest [ms]] [--dump out.bmp] [--no-delay] [--help]
```

| 参数 | 说明 |
| :--- | :--- |
| `--config <file>` | 指定工况配置；命令行参数优先于配置文件 |
| `--selftest [ms]` | 不开窗口运行到画面稳定（默认 5000 ms），仅当全部像素带刷完才返回 0，可直接用于 CI |
| `--dump out.bmp` | 把当前画面存为 BMP，可与 `--selftest` 叠加 |
| `--no-delay` | 所有模拟延迟清零，用主机速度跑 |
| `--help` | 打印用法 |

不带参数直接运行会弹出窗口，可用 `ESC` 退出，鼠标/键盘事件会转给 `sim_demo.c` 的界面。

### 配置文件字段

| 段 | 键 | 说明 |
| :--- | :--- | :--- |
| `[display]` | `width` / `height` | 屏幕分辨率（像素） |
| | `band_lines` | 像素带行数，`0` 表示按屏高取 1/8 |
| `[timing]` | `flash_latency_us` | 每次取字模的耗时（模拟外部 SPI Flash 随机读） |
| | `raster_latency_us` | 字形光栅化耗时，只在走 GDI 回退路径的缓存未命中时付出 |
| | `flush_latency_us` | 每块像素带刷屏耗时（模拟 SPI/DMA 传输） |
| | `busy_wait` | `1` 忙等（裸机）、`0` Sleep（让出 CPU） |
| `[font]` | `path` | `.hex` 字库路径，相对路径按配置文件所在目录解析 |
| `[log]` | `verbose` | 输出详细日志 |

## 新增测试的约定

1. 文件名形如 `test_<主题>.c`，放入 `tests/unit/`；依赖 Win32 的测试放 `tests/sim/`。
2. 在 `tests/CMakeLists.txt` 中注册：`add_executable()` + `target_link_libraries()`
   （链接 `leanGL_test_blocking` 或 `leanGL_test_async`）+ `add_test()` +
   `leanGL_apply_build_settings()`。
3. 文件开头添加与库源码一致的 GPL 版权声明块。
4. 编码 UTF-8 无 BOM，换行 LF（仓库根 `.gitattributes` 的 `* text=auto eol=lf` 强制）。
5. 测试须可重复运行、不依赖执行顺序，且不写入仓库内的源文件；需要输出文件时写到
   `${CMAKE_CURRENT_BINARY_DIR}`。

## 字库资产与 SIL OFL 1.1

### 文件

| 路径 | 说明 |
| :--- | :--- |
| `assets/unifont_all-18.0.01.hex` | GNU Unifont 18.0.01 的 `unifont_all` 全字库：126,743 条字形记录、8,328,396 字节（约 8 MB），**未经裁剪**；加载时整份读入内存并建立按码点排序的二分索引 |
| `OFL-1.1.txt` | SIL Open Font License 1.1 全文 |

上游：GNU Unifont — <https://unifoundry.com/unifont/>（作者 Roman Czyborra，
维护者 Paul Hardy）。

### 许可

GNU Unifont 为**双许可**：

1. **SIL Open Font License 1.1**（OFL-1.1，<https://openfontlicense.org/>）；
2. GNU GPL v2 或更高版本，并附带**字体嵌入例外**（font embedding exception）。

本仓库中的这份字库副本**按 OFL 1.1 使用与分发**，因此必须随附协议全文——
即本目录下的 `OFL-1.1.txt`。

### 使用 OFL 1.1 时的义务

- **随附声明与全文**：再分发字体（或其修改版）时需保留版权与许可声明，并附带
  OFL 1.1 全文（本目录已提供）。
- **不得单独销售**：不能把字体文件本身作为商品单独出售。
- **保留字体名**：当前使用的 unifont_all 未经裁剪与修改，故保留原名 Unifont。 
  若未来有人修改了字形数据，衍生版本须按 OFL 的保留字体名条款处理，
  不得与上游名称混淆；具体以上游发布说明与本目录的 OFL 全文为准。
- **许可不得改换**：字体或其修改版不得改用其它许可证再发布；但把字体**嵌入**文档或
  软件中，不会使该文档/软件受 OFL 约束。
- **无担保**：字体按 “as is” 提供，不附带任何担保。

### 与本项目 GPL-3.0-or-later 的关系

LeanGL 库本体以 GPL-3.0-or-later 发布，而 OFL 1.1 与 GPL 并非直接兼容。两者能共存
的原因：

- 该字库**仅作为测试资产**存在于 `tests/assets/`，不参与 `leanGL` 库的编译，
  也不会进入 install / 导出的 CMake 包；
- 即使需要并入 GPL 覆盖的范围，Unifont 同时提供 GPL-2.0+（含字体嵌入例外）作为
  替代许可，而 GPLv2+ 允许升级到 GPLv3。

替换为其它字体或自建字形时，请同步更新本节以及 `OFL-1.1.txt`。

### `.hex` 格式

纯文本点阵格式，每行一条记录：

```
 <码位(十六进制)> : <点阵数据(十六进制)>
```

例如 `0041:0000000018242442427E424242420000` 表示 U+0041（`A`）。宽度由数据长度决定：

- 16 字节（32 个十六进制字符）→ 8x16 半角；
- 32 字节（64 个十六进制字符）→ 16x16 全角。

位序为每行从左到右、每字节 MSB 在前，与 `LEANGL_Glyph_t` 的约定一致，加载后无需
任何位翻转。文件以字节精确性为要，仓库 `.gitattributes` 将其标记为 `binary`，
禁止任何换行符转换。

### 字库缺失时的行为

若 `[font] path` 指向的文件不存在或格式非法，模拟器不会报错退出，而是回退到
**GDI 现场渲染**（ASCII 8x16 + CJK 16x16）并缓存结果；此时 `raster_latency_us`
才真正生效。回退保证了在没有字库资产的环境里仍能跑通用例 4–9。
