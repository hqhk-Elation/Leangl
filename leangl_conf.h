#ifndef LEANGL_CONF_H
#define LEANGL_CONF_H

/**
 * 颜色格式。当前实现支持二选一：
 *   定义 LEANGL_COLOR_FORMAT_RGB565 = 1  -> RGB565，2 字节/像素；
 *   定义 LEANGL_COLOR_FORMAT_RGB888 = 1  -> RGB888，3 字节/像素。
 * 同时定义时 RGB888 优先（见 include/leangl_colors.h 的 #if 顺序）。
 */
#define LEANGL_COLOR_FORMAT_RGB565 1

/**
 * RGB565 像素在缓冲区里的字节序。**仅当使用 RGB565 时有效**，RGB888 忽略此配置。
 *
 * 不同 LCD 控制器的像素接口对 16 位颜色的高低字节顺序要求不一样：
 *   - 多数 SPI/并口屏按“高字节在前”接收（big endian）；
 *   - 有些 DMA/FSMC 通路直接按内存顺序发送，而 MCU 是小端，于是表现为
 *     “低字节在前”（little endian）。
 *
 * 0：低字节在前（little endian）—— 与 uint16_t 在内存中的自然布局一致。
 * 1：高字节在前（big endian）  —— 多数 LCD 控制器的期望顺序。
 *
 * 配置错了的典型现象是颜色“蓝红互换、绿色错位”：例如纯红 0xF800 发成
 * 0x00F8 后会被屏解释成一个偏蓝的颜色。调这个选项即可修正，无需改代码。
 *
 * 推荐先试 1（高字节在前），若红蓝互换再改回 0。
 */
#ifndef LEANGL_RGB565_BIG_ENDIAN
#define LEANGL_RGB565_BIG_ENDIAN 1
#endif

/**
 * 字形缓存条目数。
 *
 * glyph_cb（取字模回调）通常是整条绘制路径上最贵的一步：字库可能存放在
 * 外部 SPI Flash 里，一次取模就是一次总线传输。而同一个字符在同一帧里往往
 * 会被取模多次——测量文本宽度一次、分块刷新时每一块各一次、滚动标签还要
 * 画两份——回调次数会被放大好几倍。
 *
 * 开启缓存后，同一个码点在缓存条目内只调用一次 glyph_cb，之后直接命中。
 * 缓存按“先进先出”替换，字体变更（LEANGL_Set_Font）时整体失效。
 *
 * 0 表示关闭缓存（退回每次绘制都取模）。
 * 每条占用 sizeof(LEANGL_Glyph_t) + 8 字节左右，默认配置下约 136 字节，
 * 8 条约 1 KB。RAM 紧张时调小（或设为 0）；界面文字重复率高时可调大。
 *
 * 约定：同一字体对象对同一码点必须始终返回相同字模。若字库内容会在运行期
 * 变化，请重新调用 LEANGL_Set_Font() 使缓存失效。
 *
 * 上限 255（索引用 uint8_t）。
 */
#ifndef LEANGL_GLYPH_CACHE_SIZE
#define LEANGL_GLYPH_CACHE_SIZE 8
#endif

/** 事件处理周期（毫秒），LEANGL_Handler() 按此周期调用控件的 event_cb。 */
#define LEANGL_EVENT_PERIOD_MS 10

/**
 * 刷新回调的工作方式。
 *
 * 0：阻塞式（默认）。flush_cb 返回时像素带已经发送完毕，库在回调返回后立即
 *    释放缓冲区，调用端不需要（也不应该）在回调里调用 LEANGL_Buf_Ready()。
 * 1：异步式（DMA）。flush_cb 只负责启动传输，传输完成前库不会复用该缓冲区；
 *    必须在传输完成中断里调用 LEANGL_Buf_Ready()，且完成顺序要与发起顺序一致
 *    （单通道 DMA 天然如此），否则刷新会停在该缓冲区上。
 *
 * 只有异步模式才能真正做到“上一块还在发、下一块已经画”。
 */
#ifndef LEANGL_FLUSH_ASYNC
#define LEANGL_FLUSH_ASYNC 1
#endif

#endif