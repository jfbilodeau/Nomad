add_library(nomad-system SHARED
    ${NOMAD_SOURCE_DIR}/nomad/system/SystemFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/SystemModule.cpp
)

nomad_configure_library_target(nomad-system)
set_target_properties(nomad-system PROPERTIES OUTPUT_NAME "system.nomad")
target_link_libraries(nomad-system PUBLIC Nomad::Language)
