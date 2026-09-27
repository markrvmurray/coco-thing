cmake_minimum_required(VERSION 3.21)

project(coco-thing VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The display and the byte-stream interpreter are the pico-thing-video
# firmware's own PORTABLE translation units, compiled unchanged from its tree
# (as usim does), so this terminal draws exactly what the DVI console draws.
set(PICOVIDEO "$ENV{HOME}/Documents/PICO/pico-thing-video" CACHE PATH
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
    src/fw_stores.cpp
    src/fw_platform.cpp
)
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
    src/fontface.cpp
    src/fontface.h
    src/prefs.cpp
    src/prefs.h
)
target_include_directories(termcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(termcore PUBLIC pvcore Qt6::Gui Qt6::SerialPort)

qt_add_executable(coco-thing
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/screen.cpp
    src/screen.h
    src/keymap.cpp
    src/keymap.h
    src/prefsdialog.cpp
    src/prefsdialog.h
)
target_link_libraries(coco-thing PRIVATE termcore Qt6::Widgets)
set_target_properties(coco-thing PROPERTIES
    MACOSX_BUNDLE ON
)

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
set_tests_properties(fontface PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(test_screen tests/test_screen.cpp src/screen.cpp src/keymap.cpp)
target_link_libraries(test_screen PRIVATE termcore Qt6::Widgets)
add_test(NAME screen COMMAND test_screen)
set_tests_properties(screen PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
