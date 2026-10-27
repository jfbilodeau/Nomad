add_library(nomad-log SHARED
    ${NOMAD_SOURCE_DIR}/nomad/log/LogModule.cpp
)

nomad_configure_library_target(nomad-log)
target_link_libraries(nomad-log PRIVATE Nomad::Language)
