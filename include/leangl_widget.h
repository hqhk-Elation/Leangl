#ifndef LEANGL_WIDGET_H
#define LEANGL_WIDGET_H

#include "leangl_types.h"
#include <stdint.h>

/**
 * @file    leangl_widget.h
 * @brief   控件树：父子层级、事件处理与绘制顺序。
 *
 * 控件组成一棵树。绘制时父控件先画、子控件后画，因此子控件覆盖在父控件
 * 之上；同一父控件下按添加顺序绘制，后添加的在上层。父控件的 visible 为 0
 * 时，整棵子树都不绘制。
 *
 * 所有控件的 area 仍然是屏幕绝对坐标（沿用库的坐标约定）。父子关系只影响
 * 绘制顺序、可见性继承和移除范围，不做坐标换算，也暂不按父区域裁剪子控件。
 *
 * 具体控件（标签、按钮等）把自己的结构体定义在 widgets 目录下，把
 * LEANGL_Widget_t 作为第一个成员，并实现 draw_cb / event_cb。
 */

/* 前置声明：回调签名需要引用该标签，但结构体在回调之后定义。 */
struct LEANGL_Widget;

/** @brief 控件绘制回调，在控件区域内使用屏幕绝对坐标绘制。 */
typedef void (*LEANGL_Widget_Draw_cb)(struct LEANGL_Widget *w);

/** @brief 控件事件回调，返回 1 表示控件内容发生变化、需要重绘。 */
typedef uint8_t (*LEANGL_Widget_Event_cb)(struct LEANGL_Widget *w);

/**
 * @brief 控件基类（树节点）。
 *
 * 自定义控件把本结构体放在最前面，即可直接传给控件树相关函数。
 * parent / first_child / last_child / next_sibling 由库维护，不要直接修改。
 *
 * 注：本结构体含指向自身的回调，必须有标签名供回调签名引用，因此写成
 *     带标签的单条 typedef，而不是完全匿名的 typedef struct {}。
 */
typedef struct LEANGL_Widget {
    LEANGL_Rect_t area;              /**< 控件在屏幕上的区域，闭区间 */
    uint8_t visible;                 /**< 非 0 表示参与绘制；0 同时隐藏整棵子树 */
    LEANGL_Widget_Draw_cb draw_cb;   /**< 绘制回调，可为 NULL */
    LEANGL_Widget_Event_cb event_cb; /**< 事件回调，可为 NULL */
    struct LEANGL_Widget *parent;    /**< 父控件；NULL 表示挂在根节点下 */
    struct LEANGL_Widget *first_child;  /**< 第一个子控件 */
    struct LEANGL_Widget *last_child;   /**< 最后一个子控件 */
    struct LEANGL_Widget *next_sibling; /**< 同一父控件下的下一个兄弟 */
} LEANGL_Widget_t;

/**
 * @brief 初始化控件基类成员。
 *
 * 只用于新建控件；不要对已经挂到树里的控件调用。初始化后控件默认可见，
 * 但不会自动加入控件树。
 *
 * @param w       控件实例。
 * @param area    控件区域；传 NULL 时区域清零。
 * @param draw_cb 绘制回调，可为 NULL。
 * @param event_cb 事件回调，可为 NULL。
 */
void LEANGL_Widget_Init(LEANGL_Widget_t *w, const LEANGL_Rect_t *area,
                        LEANGL_Widget_Draw_cb draw_cb,
                        LEANGL_Widget_Event_cb event_cb);

/** @brief 把控件挂到根节点的末尾，并把它的区域标为脏。 */
void LEANGL_Widget_Add(LEANGL_Widget_t *w);

/**
 * @brief 把控件挂到指定父控件的末尾。
 *
 * 如果 child 已经挂在别的父控件下，会先摘下来再挂到 parent 末尾，
 * 因此也可以用它调整层级或把控件移到同级的最后（最上层）。
 * 不允许把祖先控件挂到自己的后代下，这种调用会被忽略。
 * parent 传 NULL 时等价于 LEANGL_Widget_Add()。
 *
 * @param parent 父控件。
 * @param child  要挂入的控件。
 */
void LEANGL_Widget_Add_Child(LEANGL_Widget_t *parent, LEANGL_Widget_t *child);

/**
 * @brief 把控件连同整棵子树从父控件上摘下，并标脏其覆盖区域。
 *
 * 子树保持完整，可以再挂到别的父控件下。隐藏/删除控件后，原区域会被标脏，
 * 由下一帧重画背景。
 */
void LEANGL_Widget_Remove(LEANGL_Widget_t *w);

/** @brief 把控件当前区域加入脏区，用于外部修改内容后主动触发重绘。 */
void LEANGL_Widget_Invalidate(LEANGL_Widget_t *w);

/**
 * @brief 递归遍历整棵树，调用每个控件的 event_cb。
 *
 * 返回 1 的控件会自动标脏。遍历顺序是父控件先于子控件，同级按添加顺序。
 */
void LEANGL_Widget_Process(void);

/**
 * @brief 按树序绘制与屏幕行区间 [y0, y1] 相交的控件。
 *
 * 父控件先画、子控件后画，因此子控件覆盖在父控件之上。不可见控件的
 * 整棵子树都会被跳过。由 LEANGL_Flush() 在每一块脏区上调用。
 */
void LEANGL_Widget_Draw(uint16_t y0, uint16_t y1);

#endif
