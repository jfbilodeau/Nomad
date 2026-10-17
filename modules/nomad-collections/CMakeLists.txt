add_library(nomad-collections SHARED
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array1dObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array2dObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array3dObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ListObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/MapObject.hpp

    ${NOMAD_SOURCE_DIR}/nomad/collections/CollectionsModule.cpp
)

nomad_configure_library_target(nomad-collections)
target_link_libraries(nomad-collections PRIVATE Nomad::Language)
