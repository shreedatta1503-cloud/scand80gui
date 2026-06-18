# ============================================================================
# QGroundControl Windows Bootstrap Launcher
# ----------------------------------------------------------------------------
# A standalone, dependency-free Win32 executable that takes over the
# QGroundControl.exe name. It verifies/repairs the runtime (Qt DLLs, plugins,
# VC++ runtime, etc.) then spawns the renamed Qt app (QGroundControlApp.exe).
#
# This target deliberately:
#   * links the CRT statically (/MT) so it runs on a clean machine,
#   * uses NO Qt and NO third-party libraries (only the Win32 SDK),
#   * does NOT inherit QGC's warnings-as-errors flags (it is platform glue).
#
# It is only added to the build when QGC_WINDOWS_BOOTSTRAP=ON (see the top-level
# CMakeLists.txt and cmake/CustomOptions.cmake).
# ============================================================================

if(NOT (WIN32 AND MSVC))
    message(FATAL_ERROR "QGC: the bootstrap launcher requires Windows + MSVC")
endif()

set(_launcher_sources
    src/main.cpp
    src/Orchestrator.cpp
    src/Config.cpp
    src/Json.cpp
    src/Common.cpp
    src/Detector.cpp
    src/Downloader.cpp
    src/Connectivity.cpp
    src/Hash.cpp
    src/Zip.cpp
    src/Installer.cpp
    src/SplashWindow.cpp
    src/Logger.cpp
    src/ProcessLauncher.cpp
    src/Elevation.cpp
)

# Windows version-info / icon / application-manifest resource.
set(QGC_LAUNCHER_VERSION_COMMA "${CMAKE_PROJECT_VERSION_MAJOR},${CMAKE_PROJECT_VERSION_MINOR},${CMAKE_PROJECT_VERSION_PATCH},0")
set(QGC_LAUNCHER_VERSION_STR   "${CMAKE_PROJECT_VERSION}")
set(QGC_LAUNCHER_ICON_PATH     "${QGC_WINDOWS_ICON_PATH}")
set(QGC_LAUNCHER_ORG_NAME      "${QGC_ORG_NAME}")
set(QGC_LAUNCHER_APP_NAME      "${CMAKE_PROJECT_NAME}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Launcher.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Launcher.rc"
    @ONLY
)
# Copy the application manifest next to the generated .rc so its relative
# reference (1 24 "app.manifest") resolves at rc-compile time.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    "${CMAKE_CURRENT_BINARY_DIR}/app.manifest"
    COPYONLY
)

add_executable(qgc_launcher WIN32
    ${_launcher_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/Launcher.rc"
)

target_include_directories(qgc_launcher PRIVATE src)
target_compile_features(qgc_launcher PRIVATE cxx_std_20)

# Statically link the MSVC runtime -> a dependency-free launcher. Requires
# CMP0091=NEW (default for CMake >= 3.15 / this project's 3.25+ minimum).
set_target_properties(qgc_launcher PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "${CMAKE_PROJECT_NAME}"      # produces QGroundControl.exe
)

# Plain warnings — intentionally NOT qgc_apply_warning_flags() (no -Werror).
target_compile_options(qgc_launcher PRIVATE /W4 /permissive- /EHsc)

# The application manifest is embedded via the .rc (1 24 "app.manifest"); tell
# the linker not to also generate/embed its own, which would conflict.
target_link_options(qgc_launcher PRIVATE /MANIFEST:NO)
target_compile_definitions(qgc_launcher PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

# Win32 SDK import libraries used by the launcher modules.
target_link_libraries(qgc_launcher PRIVATE
    winhttp wininet bcrypt crypt32 wintrust
    comctl32 shell32 shlwapi advapi32 user32 gdi32 ole32 version
)

# The launcher is statically linked and needs no Qt deployment. Install it into
# the same bin/ directory as the Qt app + windeployqt output.
install(TARGETS qgc_launcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

message(STATUS "QGC: bootstrap launcher target 'qgc_launcher' -> ${CMAKE_PROJECT_NAME}.exe (static CRT)")
