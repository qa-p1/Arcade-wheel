cmake_minimum_required(VERSION 3.25)
project(ArcadeWheel VERSION 0.2.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets Network)

add_library(arcade_core STATIC
    src/platform/PlatformBackend.h
    src/config/ConfigStore.h src/config/ConfigStore.cpp
    src/core/WheelLogic.h src/core/WheelLogic.cpp
    src/core/WheelController.h src/core/WheelController.cpp
    src/core/IconImageProvider.h src/core/IconImageProvider.cpp
    src/actions/ActionDispatcher.h src/actions/ActionDispatcher.cpp
    src/providers/ActionProvider.h
    src/providers/ArcadeBoxProvider.h src/providers/ArcadeBoxProvider.cpp
    src/link/ArcadeLink.h src/link/ArcadeLink.cpp
    src/link/WheelLink.h src/link/WheelLink.cpp
)
target_include_directories(arcade_core PUBLIC src)
target_link_libraries(arcade_core PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network)

qt_add_executable(arcade-wheel src/main.cpp)
set_target_properties(arcade-wheel PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Arcade Wheel"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.arcadewheel.ArcadeWheel"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
target_compile_definitions(arcade-wheel PRIVATE ARCADE_VERSION="${PROJECT_VERSION}")
target_link_libraries(arcade-wheel PRIVATE arcade_core Qt6::Widgets Qt6::QuickControls2 Qt6::Network)

if(WIN32)
    target_sources(arcade-wheel PRIVATE
        src/platform/windows/WindowsBackend.h src/platform/windows/WindowsBackend.cpp)
    target_link_libraries(arcade-wheel PRIVATE user32 shell32 ole32 uuid advapi32 PowrProf gdi32)
elseif(APPLE)
    add_library(arcade_macos STATIC
        src/platform/macos/MacOSBackend.h src/platform/macos/MacOSBackend.mm)
    target_link_libraries(arcade_macos PUBLIC arcade_core
        "-framework AppKit" "-framework Carbon" "-framework ServiceManagement")
    target_link_libraries(arcade-wheel PRIVATE arcade_macos)
    set_target_properties(arcade-wheel PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")
else()
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
    option(ARCADE_REQUIRE_LAYERSHELLQT "Require LayerShellQt for the Linux Wayland overlay" ON)
    find_package(LayerShellQt 6.4 QUIET)
    target_sources(arcade-wheel PRIVATE
        src/platform/linux/LinuxBackend.h src/platform/linux/LinuxBackend.cpp)
    target_link_libraries(arcade-wheel PRIVATE Qt6::DBus PkgConfig::GIO)
    if(TARGET LayerShellQt::Interface)
        target_link_libraries(arcade-wheel PRIVATE LayerShellQt::Interface)
        target_compile_definitions(arcade-wheel PRIVATE ARCADE_HAVE_LAYERSHELLQT=1)
    else()
        if(ARCADE_REQUIRE_LAYERSHELLQT)
            message(FATAL_ERROR "LayerShellQt 6.4+ is required for the Linux Wayland overlay. Install layer-shell-qt or set ARCADE_REQUIRE_LAYERSHELLQT=OFF for a development-only build.")
        endif()
        message(WARNING "LayerShellQt is missing. This development build cannot provide a reliable Wayland overlay.")
    endif()
endif()

qt_add_resources(arcade-wheel arcade_qml PREFIX "/" FILES
    qml/wheel/WheelView.qml
    qml/wheel/Overlay.qml
    qml/settings/Settings.qml
    qml/settings/ActionPicker.qml
    qml/settings/CenterGesturesPage.qml
    qml/components/UiButton.qml
    qml/components/SettingSlider.qml
    qml/components/SettingToggle.qml
    assets/arcade-wheel.svg
    assets/arcade-wheel.png
)

install(TARGETS arcade-wheel RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(FILES LICENSE README.md DESTINATION share/doc/arcade-wheel)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/com.arcadewheel.ArcadeWheel.desktop DESTINATION share/applications)
    install(FILES assets/arcade-wheel.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(test_name IN ITEMS WheelLogic ConfigStore ActionDispatcher WheelController)
        qt_add_executable(test_${test_name} tests/tst_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE arcade_core Qt6::Test)
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()
    # Arcade Link conformance vectors, vendored from Arcade-link/spec/vectors.
    qt_add_executable(test_LinkVectors tests/tst_LinkVectors.cpp)
    target_link_libraries(test_LinkVectors PRIVATE arcade_core Qt6::Test)
    target_compile_definitions(test_LinkVectors PRIVATE ARCADE_LINK_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/tests/link-vectors")
    add_test(NAME LinkVectors COMMAND test_LinkVectors)
    # Match the application's Material style. The native macOS style expects
    # Cocoa windows and cannot be used with the offscreen test platform.
    set_tests_properties(WheelController PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Material;ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT=1")
    if(WIN32)
        qt_add_executable(test_WindowsTrigger tests/tst_WindowsTrigger.cpp
            src/platform/windows/WindowsBackend.h src/platform/windows/WindowsBackend.cpp)
        target_link_libraries(test_WindowsTrigger PRIVATE arcade_core Qt6::Test user32 shell32 ole32 uuid advapi32 PowrProf gdi32)
        add_test(NAME WindowsTrigger COMMAND test_WindowsTrigger)
        set_tests_properties(WindowsTrigger PROPERTIES TIMEOUT 60)
    endif()
    if(APPLE)
        qt_add_executable(test_MacOSTrigger tests/tst_MacOSTrigger.cpp)
        target_link_libraries(test_MacOSTrigger PRIVATE arcade_macos Qt6::Test)
        add_test(NAME MacOSTrigger COMMAND test_MacOSTrigger)
    endif()
    if(UNIX AND NOT APPLE)
        qt_add_executable(test_LinuxLaunch tests/tst_LinuxLaunch.cpp
            src/platform/linux/LinuxBackend.h src/platform/linux/LinuxBackend.cpp)
        target_link_libraries(test_LinuxLaunch PRIVATE arcade_core Qt6::Test Qt6::DBus Qt6::Network PkgConfig::GIO)
        add_test(NAME LinuxLaunch COMMAND test_LinuxLaunch)
        set_tests_properties(LinuxLaunch PROPERTIES ENVIRONMENT "ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT=1")
        find_program(DBUS_RUN_SESSION dbus-run-session)
        if(DBUS_RUN_SESSION)
            qt_add_executable(test_LinuxTrigger tests/tst_LinuxTrigger.cpp
                src/platform/linux/LinuxBackend.h src/platform/linux/LinuxBackend.cpp)
            target_link_libraries(test_LinuxTrigger PRIVATE arcade_core Qt6::Test Qt6::DBus Qt6::Network PkgConfig::GIO)
            add_test(NAME LinuxTrigger COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:test_LinuxTrigger>)
        endif()
    endif()
endif()
