# ============================================================================
# Security Module
# Application-level authentication / lock (password gate + change-password).
# Sources compile into the main application target so AppLockManager's
# QML_ELEMENT registers into the QGroundControl QML module.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        AppLockManager.cc
        AppLockManager.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
