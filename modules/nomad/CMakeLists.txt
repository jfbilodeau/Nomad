add_library(nomad SHARED
    ${NOMAD_SOURCE_DIR}/nomad/geometry/Circle.cpp
    ${NOMAD_SOURCE_DIR}/nomad/geometry/CircleF.cpp
    ${NOMAD_SOURCE_DIR}/nomad/geometry/Intersection.cpp
    ${NOMAD_SOURCE_DIR}/nomad/geometry/Point.cpp
    ${NOMAD_SOURCE_DIR}/nomad/geometry/PointF.cpp
    ${NOMAD_SOURCE_DIR}/nomad/geometry/Rectangle.cpp
    ${NOMAD_SOURCE_DIR}/nomad/geometry/RectangleF.cpp

    ${NOMAD_SOURCE_DIR}/nomad/lsp/LanguageServer.cpp

    ${NOMAD_SOURCE_DIR}/nomad/project/ProjectConfiguration.cpp
    ${NOMAD_SOURCE_DIR}/nomad/project/ProjectInitializer.cpp
    ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp

    ${NOMAD_SOURCE_DIR}/nomad/system/ProjectSession.cpp
)
add_library(Nomad::Engine ALIAS nomad)
nomad_configure_library_target(nomad)
target_compile_definitions(nomad PRIVATE NOMAD_PACKAGE_PLATFORM="${NOMAD_PACKAGE_PLATFORM}")

target_include_directories(nomad PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
    ${dearimgui_SOURCE_DIR}
    $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(nomad
    PUBLIC Nomad::Language
    PRIVATE archive tomlplusplus::tomlplusplus Boost::json Boost::program_options
)

if(MSVC)
    # Boost.DLL's Windows path lookup shadows a local variable in a third-party header.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/system/ProjectSession.cpp PROPERTIES COMPILE_OPTIONS "/wd4456")
else()
    # Boost.JSON templates instantiate conversion warnings outside Nomad source.
    set_source_files_properties(
        ${NOMAD_SOURCE_DIR}/nomad/lsp/LanguageServer.cpp
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion"
    )
endif()
