cmake_minimum_required(VERSION 3.20)

project(
    matching_engine
    VERSION 0.1.0
    DESCRIPTION "C++ mock stock trading matching engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

find_package(Threads REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(nlohmann_json 3.10 REQUIRED)

add_subdirectory(engine)
add_subdirectory(router)
add_subdirectory(app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
