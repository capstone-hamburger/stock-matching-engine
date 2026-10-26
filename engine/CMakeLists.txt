add_library(matching_engine STATIC)
add_library(matching_engine::matching_engine ALIAS matching_engine)

target_sources(
    matching_engine
    PRIVATE
        src/engine.cpp
        src/models.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/matching_engine/engine.hpp
            include/matching_engine/models.hpp
)

target_compile_features(matching_engine PUBLIC cxx_std_20)

include(${PROJECT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
enable_project_warnings(matching_engine)
