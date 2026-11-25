cmake_minimum_required(VERSION 3.21)

project(coco-thing VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The display and the byte-stream interpreter are the pico-thing-video
# firmware's own PORTABLE translation units, compiled unchanged from its tree
# (as usim does), so this terminal draws exactly what the DVI console draws.
set(PICOVIDEO "$ENV{HOME}/GitHub/PICO/pico-thing-video" CACHE PATH
    "pico-thing-video firmware tree")
if(NOT EXISTS "${PICOVIDEO}/src/video_render.cpp")
    message(FATAL_ERROR "PICOVIDEO=${PICOVIDEO} is not a pico-thing-video tree")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort)
qt_standard_project_setup()

# The firmware half: its sources plus this platform's stand-ins for the SDK
# decorations (shim/) and the PSRAM-backed stores (src/fw_stores.cpp).
add_library(pvcore STATIC
    ${PICOVIDEO}/src/video_render.cpp
    ${PICOVIDEO}/src/vid_link.cpp
    ${PICOVIDEO}/src/vid_console.cpp
    ${PICOVIDEO}/src/registers.cpp
    ${PICOVIDEO}/src/hid_cook.cpp
    src/fw_stores.cpp
    src/fw_platform.cpp
    src/fw_scroll.cpp
)
# Scroll-back intercepts the console's scrolls at the link: the renderer's
# two scroll functions are renamed in its own unit, and src/fw_scroll.cpp
# defines the originals' names in front of them.
set_source_files_properties(${PICOVIDEO}/src/video_render.cpp PROPERTIES
    COMPILE_DEFINITIONS
    "vidterm_scroll_region=fw_vidterm_scroll_region;vidterm_scroll_up=fw_vidterm_scroll_up")
target_include_directories(pvcore PUBLIC
    ${PICOVIDEO}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
)

# The serial side's model of the guest and the link to the board, shared by
# the app and the tests.
add_library(termcore STATIC
    src/terminal.cpp
    src/terminal.h
    src/link.cpp
    src/link.h
    src/linesettings.cpp
    src/linesettings.h
    src/fontface.cpp
    src/fontface.h
    src/prefs.cpp
    src/prefs.h
    src/keyboard.cpp
    src/keyboard.h
    src/mackeys.cpp
    src/mackeys.h
    src/screentext.cpp
    src/screentext.h
    src/traffic.cpp
    src/traffic.h
)
target_include_directories(termcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(termcore PUBLIC pvcore Qt6::Gui Qt6::SerialPort)

# Keys go to the firmware's HID cooker by physical position, which needs a
# table from the platform's key codes to USB usages. Only macOS has one
# (src/mackeys.cpp); its keyboard-shape query is in Carbon.
if(APPLE)
    find_library(CARBON Carbon REQUIRED)
    target_link_libraries(termcore PUBLIC ${CARBON})
else()
    message(FATAL_ERROR "no physical key table for this platform: add one "
        "beside src/mackeys.cpp")
endif()

qt_add_executable(coco-thing
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/screen.cpp
    src/screen.h
    src/prefsdialog.cpp
    src/prefsdialog.h
    src/connectdialog.cpp
    src/connectdialog.h
    src/linesettingsbox.cpp
    src/linesettingsbox.h
    src/trafficpane.cpp
    src/trafficpane.h
)
target_link_libraries(coco-thing PRIVATE termcore Qt6::Widgets)
# The icon: the Pico-Thing's "?!" logo, drawn by tools/makeicon.cpp. The
# .icns is kept in the tree, so a build does not need its font; run
# `cmake --build build --target icon` to draw it again.
set(ICON ${CMAKE_SOURCE_DIR}/resources/coco-thing.icns)
target_sources(coco-thing PRIVATE ${ICON})
set_source_files_properties(${ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(coco-thing PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_ICON_FILE coco-thing.icns
)

add_executable(makeicon EXCLUDE_FROM_ALL tools/makeicon.cpp)
target_link_libraries(makeicon PRIVATE Qt6::Gui)
set(ICON_FONT "/System/Library/Fonts/Supplemental/Comic Sans MS Bold.ttf"
    CACHE FILEPATH "The font the icon's ?! is drawn in")
add_custom_target(icon
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/coco-thing.iconset
    COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
        $<TARGET_FILE:makeicon> ${ICON_FONT}
        ${CMAKE_BINARY_DIR}/coco-thing.iconset
    COMMAND iconutil -c icns -o ${ICON} ${CMAKE_BINARY_DIR}/coco-thing.iconset
    DEPENDS makeicon
    COMMENT "Drawing the icon into resources/coco-thing.icns"
    VERBATIM)

enable_testing()
add_executable(test_terminal tests/test_terminal.cpp)
target_link_libraries(test_terminal PRIVATE termcore)
add_test(NAME terminal COMMAND test_terminal)

add_executable(test_link tests/test_link.cpp)
target_link_libraries(test_link PRIVATE termcore)
add_test(NAME link COMMAND test_link)

# Rendering a system font needs a GUI platform; offscreen needs no display.
add_executable(test_fontface tests/test_fontface.cpp)
target_link_libraries(test_fontface PRIVATE termcore)
add_test(NAME fontface COMMAND test_fontface)
# The firmware's header is the oracle for exporting a face.
target_compile_definitions(test_fontface PRIVATE
    PICOVIDEO_FACE_HEADER="${PICOVIDEO}/include/font_8x16.h")
set_tests_properties(fontface PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(test_screen tests/test_screen.cpp src/screen.cpp)
target_link_libraries(test_screen PRIVATE termcore Qt6::Widgets)
add_test(NAME screen COMMAND test_screen)
set_tests_properties(screen PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(test_keyboard tests/test_keyboard.cpp)
target_link_libraries(test_keyboard PRIVATE termcore)
add_test(NAME keyboard COMMAND test_keyboard)
set_tests_properties(keyboard PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(test_mainwindow tests/test_mainwindow.cpp src/mainwindow.cpp
    src/screen.cpp src/prefsdialog.cpp src/trafficpane.cpp
    src/connectdialog.cpp src/linesettingsbox.cpp)
target_link_libraries(test_mainwindow PRIVATE termcore Qt6::Widgets)
add_test(NAME mainwindow COMMAND test_mainwindow)
set_tests_properties(mainwindow PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(test_traffic tests/test_traffic.cpp)
target_link_libraries(test_traffic PRIVATE termcore)
# The saved C header is compiled by the compilers that build this.
target_compile_definitions(test_traffic PRIVATE
    TEST_CC="${CMAKE_C_COMPILER}" TEST_CXX="${CMAKE_CXX_COMPILER}")
add_test(NAME traffic COMMAND test_traffic)

add_executable(test_screentext tests/test_screentext.cpp)
target_link_libraries(test_screentext PRIVATE termcore)
add_test(NAME screentext COMMAND test_screentext)
