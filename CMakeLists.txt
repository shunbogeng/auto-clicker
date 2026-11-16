cmake_minimum_required(VERSION 3.20)
project(AutoClicker VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(APPLE)
  enable_language(OBJCXX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

set(AC_CORE_SOURCES
  src/core/Config.cpp
  src/core/Log.cpp
  src/core/Keys.cpp
  src/core/ImageMatch.cpp
  src/core/Task.cpp
  src/core/MouseTask.cpp
  src/core/TargetTask.cpp
  src/core/Recorder.cpp
  src/core/Hotkeys.cpp
  src/core/App.cpp
  src/ui/MainWindow.cpp
  src/ui/SettingsDialog.cpp
  src/ui/CaptureOverlay.cpp
  src/platform/Platform.cpp
)

if(WIN32)
  list(APPEND AC_CORE_SOURCES src/platform/PlatformWin.cpp)
elseif(APPLE)
  list(APPEND AC_CORE_SOURCES src/platform/PlatformMac.mm)
else()
  list(APPEND AC_CORE_SOURCES src/platform/PlatformX11.cpp)
endif()

add_library(ac_core STATIC ${AC_CORE_SOURCES})
target_include_directories(ac_core PUBLIC src)
target_link_libraries(ac_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
  ${OpenCV_LIBS}
)
target_include_directories(ac_core PUBLIC ${OpenCV_INCLUDE_DIRS})

if(WIN32)
  target_link_libraries(ac_core PUBLIC user32 gdi32 shcore)
elseif(APPLE)
  target_link_libraries(ac_core PUBLIC
    "-framework Cocoa" "-framework ApplicationServices" "-framework CoreGraphics")
else()
  find_package(X11 REQUIRED)
  find_package(Threads REQUIRED)
  target_link_libraries(ac_core PUBLIC
    X11::X11 X11::Xtst X11::Xcomposite Threads::Threads)
endif()

add_executable(AutoClicker src/main.cpp)
target_link_libraries(AutoClicker PRIVATE ac_core)
set_target_properties(AutoClicker PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME "AutoClicker"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.autoclicker"
)

# ---- tests ----------------------------------------------------------------
add_executable(ac_target_window tests/target_window.cpp)
target_link_libraries(ac_target_window PRIVATE ac_core)

add_executable(ac_selftest tests/selftest.cpp)
target_link_libraries(ac_selftest PRIVATE ac_core)

add_executable(ac_e2e tests/e2e.cpp)
target_link_libraries(ac_e2e PRIVATE ac_core)

# ---- install --------------------------------------------------------------
install(TARGETS AutoClicker RUNTIME DESTINATION bin)
if(APPLE)
  install(TARGETS AutoClicker BUNDLE DESTINATION .)
endif()
