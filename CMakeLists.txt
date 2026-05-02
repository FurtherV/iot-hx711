# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(iot_hx711)

target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/index.html" TEXT RENAME_TO "webui_index_html")
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/script.mjs" TEXT RENAME_TO "webui_script_mjs")
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/style.css" TEXT RENAME_TO "webui_style_css")
