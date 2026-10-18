set(CMAKE_AUTOMOC ON)

qt_add_executable(openarchitect
    main.cpp
    Workspace.h Workspace.cpp
    SampleHouse.h SampleHouse.cpp
    Mesh.h Mesh.cpp
    MainWindow.h MainWindow.cpp
    PlanView.h PlanView.cpp
    ModelView3D.h ModelView3D.cpp
    PropertiesPanel.h PropertiesPanel.cpp
    LoadsPanel.h LoadsPanel.cpp
    BeamSizerDialog.h BeamSizerDialog.cpp
)

target_link_libraries(openarchitect PRIVATE
    oa::core
    oa::structural
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

target_compile_definitions(openarchitect PRIVATE OA_VERSION="${PROJECT_VERSION}")

set_target_properties(openarchitect PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "OpenArchitect"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.openarchitect.OpenArchitect"
)

install(TARGETS openarchitect
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
