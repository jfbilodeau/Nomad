add_library(nomad-os SHARED
    ${NOMAD_SOURCE_DIR}/nomad/os/OsModule.cpp
)

nomad_configure_library_target(nomad-os)
set_target_properties(nomad-os PROPERTIES OUTPUT_NAME "os.nomad")
target_link_libraries(nomad-os PRIVATE Nomad::Language nomad-collections Boost::dll Boost::nowide)

if(MSVC)
    # Boost.DLL's Windows path lookup shadows a local variable in a third-party header.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/os/OsModule.cpp PROPERTIES COMPILE_OPTIONS "/wd4456")
endif()
