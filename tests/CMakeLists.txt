add_executable(matching_engine_tests engine_test.cpp)

target_link_libraries(
    matching_engine_tests
    PRIVATE matching_engine::matching_engine
)

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
enable_project_warnings(matching_engine_tests)

add_test(NAME matching_engine.unit COMMAND matching_engine_tests)

add_executable(matching_router_tests router_test.cpp)
target_link_libraries(matching_router_tests PRIVATE
    matching_engine::router Boost::system nlohmann_json::nlohmann_json Threads::Threads
)
enable_project_warnings(matching_router_tests)
add_test(NAME matching_router.websocket COMMAND matching_router_tests)
set_tests_properties(matching_router.websocket PROPERTIES TIMEOUT 30)
