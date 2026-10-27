# 源文件清单统一定义在根 CMakeLists.txt 的 LEANGL_PORT_SOURCES 中。
add_library(leangl_port OBJECT
    ${LEANGL_PORT_SOURCES})
target_link_libraries(leangl_port PUBLIC leangl_headers)