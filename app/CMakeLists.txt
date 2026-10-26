add_executable(matching_engine_cli main.cpp)

target_link_libraries(
    matching_engine_cli
    PRIVATE matching_engine::matching_engine
)

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
enable_project_warnings(matching_engine_cli)

add_executable(matching_engine_server server.cpp)
target_link_libraries(matching_engine_server PRIVATE matching_engine::router Threads::Threads)
enable_project_warnings(matching_engine_server)
