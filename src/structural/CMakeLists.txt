add_library(oa_structural
    src/loads.cpp
    src/snow.cpp
    src/beam_analysis.cpp
    src/members.cpp
    src/sizing.cpp
    src/posts.cpp
    src/takedown.cpp
)
add_library(oa::structural ALIAS oa_structural)

target_include_directories(oa_structural PUBLIC include)
target_link_libraries(oa_structural PUBLIC oa::core)
