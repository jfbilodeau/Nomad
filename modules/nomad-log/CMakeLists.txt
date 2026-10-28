add_library(nomad-log SHARED
    ${NOMAD_SOURCE_DIR}/nomad/log/LogModule.cpp
)

nomad_configure_library_target(nomad-log)
set_target_properties(nomad-log PROPERTIES OUTPUT_NAME "log.nomad")
target_link_libraries(nomad-log PRIVATE Nomad::Language)
