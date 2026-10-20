add_library(oa_core
    src/units.cpp
    src/geometry.cpp
    src/document.cpp
    src/rooms.cpp
    src/terrain.cpp
    src/dimensions.cpp
    src/editing.cpp
    src/undo.cpp
    src/serialization.cpp
)
add_library(oa::core ALIAS oa_core)

target_include_directories(oa_core PUBLIC include)
target_link_libraries(oa_core PUBLIC nlohmann_json::nlohmann_json)
