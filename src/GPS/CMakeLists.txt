if(QGC_NO_SERIAL_LINK)
    return()
endif()

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GPSManager.cc
        GPSManager.h
        GPSProvider.cc
        GPSProvider.h
        GPSRtk.cc
        GPSRtk.h
        GPSRTKFactGroup.cc
        GPSRTKFactGroup.h
        RTCMMavlink.cc
        RTCMMavlink.h
        satellite_info.h
        sensor_gnss_relative.h
        sensor_gps.h
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

#===========================================================================#

CPMAddPackage(
    NAME px4-gpsdrivers
    GITHUB_REPOSITORY PX4/PX4-GPSDrivers
    # Pinned by upstream Stable_V5.0 ("GPS: Set Drivers to Specific Commit", 2025-12-17).
    # This commit predates the 2026-06 GPSDriverUBX `Settings`-arg change (#213) and is
    # compatible with GPSProvider.cc's 5-arg call site, so the fork's earlier workaround
    # pin (0b96958) is superseded by this upstream stable pin.
    GIT_TAG caf5158061bd10e79c9f042abb62c86bc6f3e7a7
    SOURCE_SUBDIR src
)

file(GLOB GPS_DRIVERS_SOURCES "${px4-gpsdrivers_SOURCE_DIR}/src/*")
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        definitions.h
        ${GPS_DRIVERS_SOURCES}
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE GPS_DEFINITIONS_HEADER=<${CMAKE_CURRENT_SOURCE_DIR}/definitions.h>)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${px4-gpsdrivers_SOURCE_DIR}/src)
