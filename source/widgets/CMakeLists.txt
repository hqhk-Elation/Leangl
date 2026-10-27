# 源文件清单统一定义在根 CMakeLists.txt 的 LEANGL_WIDGET_SOURCES 中。
add_library(leangl_widgets OBJECT
    ${LEANGL_WIDGET_SOURCES}
    ${PROJECT_SOURCE_DIR}/include/leangl_widget.h
    ${PROJECT_SOURCE_DIR}/include/leangl_label.h
    ${PROJECT_SOURCE_DIR}/include/leangl_button.h
    ${PROJECT_SOURCE_DIR}/include/leangl_list.h
    ${PROJECT_SOURCE_DIR}/include/leangl_scroll_label.h)
target_link_libraries(leangl_widgets PUBLIC leangl_headers)
