cmake_minimum_required(VERSION 3.25)
project(ArcadeWheel VERSION 0.4.5 LANGUAGES CXX)

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
qt_standard_project_setup(REQUIRES 6.8)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

set(ARCADE_LINK_BUILD_TESTS OFF CACHE BOOL "Use Wheel's test registration" FORCE)
add_subdirectory(src/link/qt)

add_library(arcade_core STATIC
    src/platform/PlatformBackend.h
    src/config/ConfigStore.h src/config/ConfigStore.cpp
    src/core/WheelLogic.h src/core/WheelLogic.cpp
    src/core/WheelController.h src/core/WheelController.cpp
    src/core/IconImageProvider.h src/core/IconImageProvider.cpp
    src/core/IconTheme.h src/core/IconTheme.cpp
    src/actions/ActionDispatcher.h src/actions/ActionDispatcher.cpp
    src/providers/ActionProvider.h
    src/providers/ArcadeLinkProvider.h src/providers/ArcadeLinkProvider.cpp
    src/link/ArcadeLink.h
    src/install/AppInstaller.h src/install/AppInstaller.cpp
    src/link/WheelLink.h src/link/WheelLink.cpp
    src/link/WheelInvoke.h src/link/WheelInvoke.cpp
)
target_include_directories(arcade_core PUBLIC src)
target_link_libraries(arcade_core PUBLIC arcade_link_qt Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::Network)
# WheelController's metatypes, for the QML_FOREIGN registration in wheel_ui.
qt_extract_metatypes(arcade_core)

# The Arcade design kit (vendored from Link) as a compiled static QML module,
# so qmlcachegen compiles it ahead of time and qmllint knows its types.
set(ARCADE_KIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/link/design/qml/Arcade")
file(GLOB ARCADE_KIT_FILES CONFIGURE_DEPENDS "${ARCADE_KIT_DIR}/*.qml" "${ARCADE_KIT_DIR}/*.js")
foreach(file IN LISTS ARCADE_KIT_FILES)
    get_filename_component(name "${file}" NAME)
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
    if(name STREQUAL "Theme.qml" OR name STREQUAL "Fonts.qml")
        set_source_files_properties("${file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()
qt_add_library(arcade_kit STATIC)
qt_add_qml_module(arcade_kit URI Arcade VERSION 1.0 QML_FILES ${ARCADE_KIT_FILES})
# Fonts.qml loads ../../fonts/InterVariable.ttf next to the module (/qt/qml/Arcade).
qt_add_resources(arcade_kit "arcade_inter" PREFIX "/qt/fonts" BASE "src/link/design/fonts"
    FILES src/link/design/fonts/InterVariable.ttf)
target_link_libraries(arcade_kit PRIVATE Qt6::Quick)

# Wheel's own QML, a typed module (qmllint knows WheelController).
set(WHEEL_QML_FILES
    qml/wheel/WheelView.qml
    qml/wheel/Overlay.qml
    qml/settings/SettingsWindow.qml
    qml/settings/ActionPicker.qml
    qml/settings/LinkActivity.qml
    qml/settings/ConnectedAppsPage.qml
    qml/settings/CenterGesturesPage.qml
    qml/components/WheelSlider.qml
    qml/components/WheelSwitch.qml
)
qt_add_library(wheel_ui STATIC)
qt_add_qml_module(wheel_ui URI ArcadeWheel VERSION 1.0
    QML_FILES ${WHEEL_QML_FILES}
    SOURCES src/ui/QmlTypes.h
    IMPORTS Arcade)
target_link_libraries(wheel_ui PRIVATE arcade_core arcade_kit Qt6::Quick)

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
target_link_libraries(arcade-wheel PRIVATE arcade_core wheel_uiplugin arcade_kitplugin Qt6::Widgets Qt6::QuickControls2 Qt6::Network)

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

set(ARCADE_ASSET_FILES
    assets/arcade/arcade.box.svg
    assets/arcade/arcade.lens.svg
    assets/arcade/arcade.look.svg
    assets/arcade/arcade.wheel.svg
    assets/arcade/arcade.clipboard.svg
    assets/arcade/arcade.shelf.svg
    assets/arcade/arcade.find.svg
    assets/arcade/arcade.tools.svg
    assets/arcade/tokens.json
    assets/arcade-wheel.svg
    assets/arcade-wheel.png
)
qt_add_resources(arcade-wheel arcade_assets PREFIX "/" FILES ${ARCADE_ASSET_FILES})
# The QML gate (STANDARD §10): qmllint over Wheel's module at zero warnings.
add_custom_target(qmllint_gate
    COMMAND $<TARGET_FILE:Qt6::qmllint> "@${CMAKE_BINARY_DIR}/.rcc/qmllint/wheel_ui.rsp" --max-warnings 0
    COMMENT "qmllint (Wheel QML, zero warnings)" VERBATIM)
add_dependencies(qmllint_gate wheel_ui)

install(TARGETS arcade-wheel RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(FILES LICENSE README.md DESTINATION share/doc/arcade-wheel)
install(DIRECTORY docs/user/ DESTINATION share/doc/arcade-wheel/docs/user)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/com.arcadewheel.ArcadeWheel.desktop DESTINATION share/applications)
    install(FILES assets/arcade-wheel.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(test_name IN ITEMS WheelLogic ConfigStore ActionDispatcher WheelController CommandReply)
        qt_add_executable(test_${test_name} tests/tst_${test_name}.cpp)
        target_link_libraries(test_${test_name} PRIVATE arcade_core Qt6::Test)
        add_test(NAME ${test_name} COMMAND test_${test_name})
    endforeach()
    # Arcade Link conformance vectors, vendored from Arcade-link/spec/vectors.
    qt_add_executable(test_LinkVectors tests/tst_LinkVectors.cpp)
    target_link_libraries(test_LinkVectors PRIVATE arcade_core Qt6::Test)
    target_compile_definitions(test_LinkVectors PRIVATE ARCADE_LINK_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/src/link/spec/vectors")
    add_test(NAME LinkVectors COMMAND test_LinkVectors)
    qt_add_executable(test_WheelLink tests/tst_WheelLink.cpp)
    target_link_libraries(test_WheelLink PRIVATE arcade_core Qt6::Test)
    add_test(NAME WheelLink COMMAND test_WheelLink)
    set_tests_properties(WheelLink PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT=1")
    qt_add_executable(test_AppInstaller tests/tst_AppInstaller.cpp)
    target_link_libraries(test_AppInstaller PRIVATE arcade_core Qt6::Test)
    qt_add_resources(test_AppInstaller install_test_assets PREFIX "/" FILES assets/arcade-wheel.svg assets/arcade-wheel.png)
    add_test(NAME AppInstaller COMMAND test_AppInstaller)
    set_tests_properties(AppInstaller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Every Settings page at common window sizes; ARCADE_WHEEL_SHOTS=DIR saves PNGs.
    qt_add_executable(test_SettingsLayout tests/tst_SettingsLayout.cpp)
    target_link_libraries(test_SettingsLayout PRIVATE arcade_core Qt6::Test Qt6::QuickControls2)
    qt_add_resources(test_SettingsLayout layout_assets PREFIX "/" FILES ${ARCADE_ASSET_FILES})
    # The QtTest log also goes to Testing/Temporary, which CI uploads on failure.
    add_test(NAME SettingsLayout COMMAND test_SettingsLayout -o -,txt
        -o "${CMAKE_BINARY_DIR}/Testing/Temporary/SettingsLayout.txt,txt")
    set(settings_layout_env "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Material;QT_QUICK_CONTROLS_MATERIAL_THEME=Dark;ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT=1")
    if(WIN32)
        # The offscreen platform finds no fonts on Windows unless pointed at the
        # system folder; its fallback glyphs are one em wide and every label overflows.
        string(REPLACE "\\" "/" windows_font_dir "$ENV{SystemRoot}/Fonts")
        list(APPEND settings_layout_env "QT_QPA_FONTDIR=${windows_font_dir}")
    endif()
    set_tests_properties(SettingsLayout PROPERTIES ENVIRONMENT "${settings_layout_env}")
    qt_add_executable(test_WheelTrayHost tests/tst_WheelTrayHost.cpp)
    target_link_libraries(test_WheelTrayHost PRIVATE arcade_link_qt Qt6::Test)
    add_test(NAME WheelTrayHost COMMAND test_WheelTrayHost)
    # Exercise every released install/shortcut/tray vector without modifying
    # the vendored test sources. Their native mutations use isolated roots.
    foreach(link_test IN ITEMS Vectors Install TrayHost Methods Server)
        qt_add_executable(test_ReleasedLink${link_test} src/link/qt/tests/tst_${link_test}.cpp)
        target_link_libraries(test_ReleasedLink${link_test} PRIVATE arcade_link_qt Qt6::Test)
        target_compile_definitions(test_ReleasedLink${link_test} PRIVATE
            ARCADE_LINK_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/src/link/spec/vectors"
            ARCADE_LINK_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/src/link/fixtures"
            ARCADE_LINK_INSTALL_STRINGS="${CMAKE_CURRENT_SOURCE_DIR}/src/link/assets/strings/install.json")
        add_test(NAME ReleasedLink${link_test} COMMAND test_ReleasedLink${link_test} -o -,txt)
    endforeach()
    qt_add_executable(test_ArcadeLinkProvider tests/tst_ArcadeLinkProvider.cpp)
    target_link_libraries(test_ArcadeLinkProvider PRIVATE arcade_core Qt6::Test)
    target_compile_definitions(test_ArcadeLinkProvider PRIVATE ARCADE_PROVIDER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ArcadeLinkProvider COMMAND test_ArcadeLinkProvider)
    set_tests_properties(ArcadeLinkProvider PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
        qt_add_executable(test_IconTheme tests/tst_IconTheme.cpp)
        target_link_libraries(test_IconTheme PRIVATE arcade_core Qt6::Test)
        target_compile_definitions(test_IconTheme PRIVATE ARCADE_ICON_THEME_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/icon-theme")
        add_test(NAME IconTheme COMMAND test_IconTheme)
        set_tests_properties(IconTheme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
