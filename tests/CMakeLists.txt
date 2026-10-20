add_executable(oa_tests
    test_units.cpp
    test_geometry.cpp
    test_document.cpp
    test_loads.cpp
    test_beam.cpp
    test_takedown.cpp
    test_rooms.cpp
    test_posts.cpp
    test_site.cpp
    test_editing.cpp
)
target_link_libraries(oa_tests PRIVATE oa::core oa::structural Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(oa_tests)
