set(_agricultural_spray_planner_source "${CMAKE_CURRENT_SOURCE_DIR}/../src/MissionManager/AgriculturalSprayPlanner.cc")
get_target_property(_qgc_target_sources ${CMAKE_PROJECT_NAME} SOURCES)
if(NOT _agricultural_spray_planner_source IN_LIST _qgc_target_sources)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${_agricultural_spray_planner_source})
endif()
unset(_qgc_target_sources)
unset(_agricultural_spray_planner_source)

target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE MissionManager/AgriculturalSprayComplexItemTest.cc
            MissionManager/AgriculturalSprayComplexItemTest.h
            MissionManager/AgriculturalSprayPlanLoadTest.cc
            MissionManager/AgriculturalSprayPlanLoadTest.h
            MissionManager/AgriculturalSprayPlannerTest.cc
            MissionManager/AgriculturalSprayPlannerTest.h
            MissionManager/AgriculturalSprayPluginTest.cc
            MissionManager/AgriculturalSprayPluginTest.h
            QmlUITests/AgriculturalSprayPlanViewUITest.cc
            QmlUITests/AgriculturalSprayPlanViewUITest.h
)

target_include_directories(${CMAKE_PROJECT_NAME}
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/MissionManager ${CMAKE_CURRENT_SOURCE_DIR}/QmlUITests
                                   ${CMAKE_CURRENT_SOURCE_DIR}/../src/MissionManager
)

add_qgc_test(AgriculturalSprayPlannerTest LABELS Unit MissionManager Custom)
add_qgc_test(AgriculturalSprayComplexItemTest LABELS Unit MissionManager Custom)
add_qgc_test(AgriculturalSprayPluginTest LABELS Unit MissionManager Custom)
add_qgc_test(AgriculturalSprayPlanLoadTest LABELS Unit MissionManager Custom)
add_qgc_test(AgriculturalSprayPlanViewUITest LABELS Integration MissionManager Custom NoSanitizer)
