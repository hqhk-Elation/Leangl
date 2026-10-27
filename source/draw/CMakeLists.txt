# 源文件清单统一定义在根 CMakeLists.txt 的 LEANGL_DRAW_SOURCES 中。
# 头文件列进来只为在 IDE 里可见，本身不参与编译。
add_library(leangl_draw OBJECT
    ${LEANGL_DRAW_SOURCES}
    ${PROJECT_SOURCE_DIR}/include/leangl_draw_pixel.h
    ${PROJECT_SOURCE_DIR}/include/leangl_draw_shape.h)
target_link_libraries(leangl_draw PUBLIC leangl_headers)