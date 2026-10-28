add_library(nomad-collections SHARED
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array1Type.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array1dObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array2Type.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array2dObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array3Type.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/Array3dObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ArrayIterator.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ArrayObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ArrayType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/KeyValueObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/KeyValueType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ListIterator.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ListObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ListType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/MapObject.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/MapType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ReadOnlyArrayType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ReadOnlyListType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/ReadOnlyMapType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/RegisterArrayType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/RegisterKeyValueType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/RegisterListType.hpp
    ${NOMAD_INCLUDE_DIR}/nomad/collections/RegisterMapType.hpp

    ${NOMAD_SOURCE_DIR}/nomad/collections/Array1Type.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/Array1dObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/Array2Type.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/Array2dObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/Array3Type.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/Array3dObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ArrayIterator.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ArrayObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ArrayType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/CollectionsModule.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/KeyValueObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/KeyValueType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ListIterator.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ListObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ListType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/MapObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/MapType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ReadOnlyArrayType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ReadOnlyListType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/ReadOnlyMapType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/RegisterArrayType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/RegisterKeyValueType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/RegisterListType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/collections/RegisterMapType.cpp
)

nomad_configure_library_target(nomad-collections)
set_target_properties(nomad-collections PROPERTIES OUTPUT_NAME "collections.nomad")
target_link_libraries(nomad-collections PRIVATE Nomad::Language)
