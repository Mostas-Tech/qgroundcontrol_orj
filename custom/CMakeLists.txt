# ============================================================================
# QGroundControl Custom Build Overlay
# Neutral baseline: builds identical to stock QGroundControl.
# Add company sources/resources here as customizations land
# (see custom-example/CMakeLists.txt for the full template).
# ============================================================================

message(STATUS "QGC: Adding Custom Plugin")

list(APPEND CUSTOM_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc)
set(QGC_RESOURCES
    ${QGC_RESOURCES} ${CUSTOM_RESOURCES}
    CACHE STRING "Paths to .qrc Resources" FORCE
)

set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOptions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOptions.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MissionManager/AgriculturalSprayComplexItem.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MissionManager/AgriculturalSprayComplexItem.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MissionManager/AgriculturalSprayGeometry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MissionManager/AgriculturalSprayPlanner.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MissionManager/AgriculturalSprayPlanner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlanView/AgriculturalSprayEditor.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlanView/AgriculturalSprayMapVisual.qml
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    ""
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD CUSTOMHEADER="CustomPlugin.h" CUSTOMCLASS=CustomPlugin
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_QT_COMPONENTS
    Core
    CACHE INTERNAL "" FORCE
)
