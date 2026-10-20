add_library(nomad-os SHARED
    ${NOMAD_SOURCE_DIR}/nomad/os/OsModule.cpp
)

nomad_configure_library_target(nomad-os)
target_link_libraries(nomad-os PRIVATE Nomad::Language nomad-collections Boost::dll Boost::nowide)
