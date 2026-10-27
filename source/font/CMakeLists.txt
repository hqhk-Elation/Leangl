# 源文件清单统一定义在根 CMakeLists.txt 的 LEANGL_FONT_SOURCES 中。
add_library(leangl_font OBJECT
    ${LEANGL_FONT_SOURCES}
    ${PROJECT_SOURCE_DIR}/include/leangl_font.h)
target_link_libraries(leangl_font PUBLIC leangl_headers)
