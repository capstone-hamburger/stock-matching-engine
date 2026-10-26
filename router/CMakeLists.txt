add_library(matching_router STATIC src/router.cpp src/order_protocol.cpp)
add_library(matching_engine::router ALIAS matching_router)
target_include_directories(matching_router PUBLIC include)
target_link_libraries(matching_router
    PUBLIC matching_engine::matching_engine
    PRIVATE Boost::system nlohmann_json::nlohmann_json Threads::Threads
)
include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
enable_project_warnings(matching_router)
