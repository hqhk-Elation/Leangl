/*
 * Copyright (C) 2026  黄楷翔
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LEANGL_FONT_H
#define LEANGL_FONT_H

#include <stdint.h>

/**
 * @file    leangl_font.h
 * @brief   字体描述与取字模接口。
 *
 * 库不内置任何字库：所有字形都通过 LEANGL_Font_t 里的 glyph_cb 取得。
 * 一个 LEANGL_Font_t 实例描述一种字体（字模尺寸上限 + 取模回调 + 私有数据），
 * 应用先用 LEANGL_Set_Font() 注册，之后所有文本绘制都用它。
 *
 * 典型用法（等宽点阵字库）：
 *
 *   static const uint8_t k_font8x16[] = { ... };   // 字库数据
 *
 *   static uint8_t my_glyph(const LEANGL_Font_t *font, LEANGL_CodePoint_t ch,
 *                           LEANGL_Glyph_t *out) {
 *       const uint8_t *p = font->user;             // 私有数据
 *       uint32_t index = ch - 32u;
 *       if (ch < 32u || ch > 126u) return 0;        // 无此字
 *       out->size.w = 8;                           // 宽度由回调决定
 *       out->size.h = 16;
 *       memcpy(out->bits, p + index * 16u, 16);
 *       return 1;
 *   }
 *
 *   static const LEANGL_Font_t k_font =
 *       LEANGL_FONT_INIT(16, my_glyph, k_font8x16);
 *
 *   LEANGL_Set_Font(&k_font);
 *
 * 字符编码约定：
 *   - 文本一律是 UTF-8 字节串，库负责解码；
 *   - glyph_cb 收到的 ch 是**解码后的 Unicode 码点**，不是原始字节；
 *     例如「中」（UTF-8 为 E4 B8 AD）只会触发一次回调，ch = 0x4E2D；
 *   - 无此字时回调返回 0，库会跳过该字符（不推进光标）。
 *
 * 点阵排布约定（glyph_cb 必须遵守）：
 *   - 按行从上到下存放，每行占 ceil(w / 8) 字节（w 不要求是 8 的倍数）；
 *   - 每字节最高位（bit7）对应最左边的像素，即 MSB-first；
 *   - 总长度不超过 (ceil(w/8)) * h 字节。
 */

/** 字模宽度上限（像素）。glyph_cb 返回超过此值的尺寸会被判为无此字。 */
#define LEANGL_MAX_FONT_W 32

/** 字模高度上限（像素）。 */
#define LEANGL_MAX_FONT_H 32

/** 单个字模点阵占用的最大字节数，即 ceil(32/8)*32 = 128。 */
#define LEANGL_MAX_GLYPH_BYTES (((LEANGL_MAX_FONT_W + 7) / 8) * LEANGL_MAX_FONT_H)

/**
 * @brief Unicode 码点。
 *
 * 用 32 位以覆盖完整码点空间（BMP 之外还有 emoji 等，最大 U+10FFFF）。
 * 若目标平台 RAM/ROM 紧张且只用 BMP 字符，可改成 uint16_t。
 */
typedef uint32_t LEANGL_CodePoint_t;

/** @brief LEANGL_Utf8_Next() 用来表示“已到末尾或遇到非法序列”。 */
#define LEANGL_CODEPOINT_NONE ((LEANGL_CodePoint_t)0)

/** @brief 字模尺寸（像素）。 */
typedef struct {
    uint8_t w; /**< 字模宽度（像素）。 */
    uint8_t h; /**< 字模高度（像素）。 */
} LEANGL_Font_Size_t;

/**
 * @brief 一个已取出的字模。
 *
 * 由 LEANGL_Font_Glyph() 填充并返回，调用方只读。
 */
typedef struct {
    LEANGL_Font_Size_t size;             /**< 实际宽高；w 为 0 表示无此字。 */
    uint8_t bits[LEANGL_MAX_GLYPH_BYTES];/**< 点阵数据，MSB-first，每行 ceil(w/8) 字节。 */
} LEANGL_Glyph_t;

/**
 * @brief 取字模回调。
 *
 * 由具体字库实现。库保证 buf 指向的 out 至少有 LEANGL_MAX_GLYPH_BYTES 字节
 * 的 bits 空间，回调只需按实际尺寸写入前 (ceil(w/8)) * h 字节。
 *
 * @note 库会按码点缓存回调结果（见 LEANGL_GLYPH_CACHE_SIZE），因此对同一字体
 *       对象、同一码点应始终返回相同字模；字库内容变化后请重新调用
 *       LEANGL_Set_Font() 使缓存失效。回调内部不必再做缓存。
 *
 * @param font 字体对象本身（即注册时传入的那个实例），可从 font->user 取出私有数据。
 * @param ch   要取的字符，**Unicode 码点**（不是 UTF-8 字节）。
 * @param out  输出：成功时填 size 与 bits。
 *
 * @return 1 表示成功取到字模；0 表示该字符不存在（库会跳过绘制，不影响后续字符）。
 */
/* 前置声明：回调签名需要引用该标签，但结构体在回调之后定义。 */
struct LEANGL_Font;

typedef uint8_t (*LEANGL_Font_Glyph_cb)(const struct LEANGL_Font *font,
                                        LEANGL_CodePoint_t ch,
                                        LEANGL_Glyph_t *out);

/**
 * @brief 字体描述。
 *
 * 只保留高度 h（各字符通常等高），**宽度完全由 glyph_cb 逐字提供**：中英文
 * 等比例混排时，每个字符的实际宽度从 out->size.w 读取，库不做任何假设。
 *
 * 注：本结构体含指向自身的回调，必须有标签名供回调签名引用，因此写成
 *     带标签的单条 typedef，而不是完全匿名的 typedef struct {}。
 */
typedef struct LEANGL_Font {
    uint8_t h;                     /**< 标称字模高度（像素），仅用于布局预估。 */
    LEANGL_Font_Glyph_cb glyph_cb; /**< 取字模回调，不可为 NULL。 */
    const void *user;              /**< 字库实现自用的数据指针，库不会访问。 */
} LEANGL_Font_t;

/**
 * @brief 定义一个字体的静态初始化器。
 *
 * @param _h   标称字模高度（像素）。
 * @param _cb  取字模回调。
 * @param _usr 私有数据指针，可为 NULL。
 */
#define LEANGL_FONT_INIT(_h, _cb, _usr) \
    { (uint8_t)(_h), (_cb), (_usr) }

/**
 * @brief 注册当前字体。
 *
 * 传 NULL 表示取消注册，此后所有文本都不会绘制。
 * 注册后需标脏相关控件（例如逐个调用 LEANGL_Widget_Invalidate）才会重绘。
 *
 * @param font 字体描述，通常指向一个 static const 实例；库只保存指针。
 */
void LEANGL_Set_Font(const LEANGL_Font_t *font);

/** @brief 取当前字体；未注册时返回 0。 */
const LEANGL_Font_t *LEANGL_Get_Font(void);

/** @brief 当前字体的标称字高（像素）；未注册时为 0。 */
uint8_t LEANGL_Font_Height(void);

/**
 * @brief 从 UTF-8 字节串中解出一个码点，并把指针移到下一个字符。
 *
 * 这是库内部遍历文本的唯一方式，也对外提供，方便应用做自定义排版。
 *
 * 行为约定：
 *   - 遇到 '\0' 时不移动指针，返回 LEANGL_CODEPOINT_NONE (0)；
 *   - 遇到非法字节序列时**仍然前进**（跳过 1 或若干字节），返回
 *     LEANGL_CODEPOINT_NONE，调用方不会死循环；
 *   - 合法的 4 字节序列（emoji 等）会被正确合成一个 32 位码点。
 *
 * @param pp 指向“当前字节位置指针”的指针。成功时 *pp 会前进 1~4 字节。
 *           传 NULL 或 *pp 为 NULL 时直接返回 0。
 *
 * @return 解出的码点；末尾或非法序列返回 0。
 */
LEANGL_CodePoint_t LEANGL_Utf8_Next(const char **pp);

/**
 * @brief 取一个字符的字模。
 *
 * 结果来自模块内部的小型字形缓存（条目数由 leangl_conf.h 中的
 * LEANGL_GLYPH_CACHE_SIZE 决定，默认 8 条）。**同一码点只有第一次会调用
 * glyph_cb**，之后直接命中缓存，因此测量宽度、分块刷新、滚动副本等重复取模
 * 不会反复访问字库。
 *
 * 返回的指针在下述情况之前保持有效：
 *   - 该条目被新字符挤出缓存（缓存满时按先进先出替换）；
 *   - 重新调用 LEANGL_Set_Font()。
 * 为安全起见，仍建议“取一个、立刻用”，不要跨多次取模长期保存。
 *
 * 若字库内容会在运行期变化，请在修改后重新调用 LEANGL_Set_Font() 使缓存失效。
 *
 * @param ch Unicode 码点（可用 LEANGL_Utf8_Next() 从 UTF-8 文本中解出）。
 *
 * @return 指向字模的指针；未注册字体、无此字，或回调返回的尺寸越界时返回 0。
 */
const LEANGL_Glyph_t *LEANGL_Font_Glyph(LEANGL_CodePoint_t ch);

/**
 * @brief 逐字符累加，算出整串 UTF-8 文本的像素宽度。
 *
 * 内部用 LEANGL_Utf8_Next() 解码，每个字符只查询一次字模，因此像「中」
 * 这样的 3 字节 UTF-8 字符只会算一次宽度，不会重复计数。
 * 遇到无此字的字符时按跳过处理（不计宽度），不中断统计。
 *
 * @param text 以 '\0' 结尾的 UTF-8 字符串；传 NULL 时返回 0。
 *
 * @return 文本宽度（像素）；未注册字体时为 0；总宽超过 UINT16_MAX 时钳位。
 *
 * @note 会逐字符调用 glyph_cb，成本与字符数成正比。若字库取自慢速
 *       外部存储（SPI Flash 等），请在回调内部做缓存。
 */
uint16_t LEANGL_Font_Text_Width(const char *text);

/**
 * @brief 逐字符累加，算出整串 UTF-8 文本的最大字符高度。
 *
 * 多数字体各字符等高，此时结果等于 font->h；混排不同字号时取最大值。
 *
 * @param text 以 '\0' 结尾的 UTF-8 字符串；传 NULL 时返回 0。
 *
 * @return 文本高度（像素）；未注册字体时为 0。
 */
uint8_t LEANGL_Font_Text_Height(const char *text);

#endif /* LEANGL_FONT_H */
