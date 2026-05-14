# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(iot_hx711)

set(WEBUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webui")
set(WEBUI_DIST_DIR "${WEBUI_DIR}/dist")

find_program(NODE_EXECUTABLE node)
if(NOT NODE_EXECUTABLE)
    message(FATAL_ERROR "Node.js is required to build the embedded WebUI.")
endif()

find_program(NPM_EXECUTABLE npm)
if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm is required to build the embedded WebUI.")
endif()

if(NOT EXISTS "${WEBUI_DIR}/node_modules/.bin/vite")
    message(FATAL_ERROR "WebUI dependencies are missing. Run 'npm install' in ${WEBUI_DIR}, then build again.")
endif()

set(WEBUI_DIST_FILES
    "${WEBUI_DIST_DIR}/index.html.gz"
    "${WEBUI_DIST_DIR}/api.html.gz"
    "${WEBUI_DIST_DIR}/assets/index.js.gz"
    "${WEBUI_DIST_DIR}/assets/index.css.gz"
)

set(WEBUI_BUILD_DEPS
    "${CMAKE_CURRENT_SOURCE_DIR}/api/openapi.yaml"
    "${WEBUI_DIR}/package.json"
    "${WEBUI_DIR}/vite.config.mjs"
    "${WEBUI_DIR}/scripts/gzip-dist.mjs"
    "${WEBUI_DIR}/src/index.html"
    "${WEBUI_DIR}/src/script.mjs"
    "${WEBUI_DIR}/src/style.css"
    "${WEBUI_DIR}/src/firmware-logo.svg"
)

if(EXISTS "${WEBUI_DIR}/package-lock.json")
    list(APPEND WEBUI_BUILD_DEPS "${WEBUI_DIR}/package-lock.json")
endif()

add_custom_command(
    OUTPUT ${WEBUI_DIST_FILES}
    COMMAND "${NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${WEBUI_DIR}"
    DEPENDS ${WEBUI_BUILD_DEPS}
    COMMENT "Building Vite WebUI and gzip assets"
    VERBATIM
)

add_custom_target(webui_dist DEPENDS ${WEBUI_DIST_FILES})

target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/dist/index.html.gz" BINARY RENAME_TO "webui_index_html_gz" DEPENDS webui_dist)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/dist/api.html.gz" BINARY RENAME_TO "webui_api_html_gz" DEPENDS webui_dist)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/dist/assets/index.js.gz" BINARY RENAME_TO "webui_assets_index_js_gz" DEPENDS webui_dist)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "webui/dist/assets/index.css.gz" BINARY RENAME_TO "webui_assets_index_css_gz" DEPENDS webui_dist)
