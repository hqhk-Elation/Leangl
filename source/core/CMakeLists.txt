# 源文件清单统一定义在根 CMakeLists.txt 的 LEANGL_CORE_SOURCES 中。
add_library(leangl_core OBJECT
    ${LEANGL_CORE_SOURCES}
    ${PROJECT_SOURCE_DIR}/include/leangl_dirty.h
    ${PROJECT_SOURCE_DIR}/include/leangl_flush.h)
target_link_libraries(leangl_core PUBLIC leangl_headers)