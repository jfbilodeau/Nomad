include("${CMAKE_CURRENT_SOURCE_DIR}/generated/nomad-module.cmake")

add_library(nomad-log SHARED
    ${NOMAD_BINDING_HEADERS}
    ${NOMAD_BINDING_SOURCES}
    ${NOMAD_SOURCE_DIR}/nomad/log/LogModule.cpp
)

nomad_configure_library_target(nomad-log)
set_target_properties(nomad-log PROPERTIES OUTPUT_NAME "log.nomad")
target_include_directories(nomad-log PRIVATE "${NOMAD_BINDING_INCLUDE_DIRECTORY}")
target_link_libraries(nomad-log PRIVATE Nomad::Language)
