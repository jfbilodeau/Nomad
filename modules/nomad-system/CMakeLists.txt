add_library(nomad-system SHARED
    ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/SystemModule.cpp
)

nomad_configure_library_target(nomad-system)
target_link_libraries(nomad-system PUBLIC Nomad::Language)
