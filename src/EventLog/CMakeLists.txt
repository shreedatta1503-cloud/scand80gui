# ============================================================================
# Event Log Module
# Asynchronous, thread-safe timestamped event logging subsystem.
# Sources compile into the main application target (like Settings/QmlControls),
# so EventLogger's QML_ELEMENT registers into the QGroundControl QML module and
# the code has access to all accumulated include directories.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        EventLogger.cc
        EventLogger.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
