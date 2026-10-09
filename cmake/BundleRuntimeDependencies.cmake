if(NOT DEFINED NOMAD_RUNTIME_DESTINATION)
    message(FATAL_ERROR "NOMAD_RUNTIME_DESTINATION is required")
endif()

file(MAKE_DIRECTORY "${NOMAD_RUNTIME_DESTINATION}")

if(DEFINED NOMAD_RUNTIME AND NOT NOMAD_RUNTIME STREQUAL "")
    get_filename_component(NOMAD_RUNTIME_NAME "${NOMAD_RUNTIME}" NAME)
    file(
        COPY_FILE
        "${NOMAD_RUNTIME}"
        "${NOMAD_RUNTIME_DESTINATION}/${NOMAD_RUNTIME_NAME}"
        ONLY_IF_DIFFERENT
    )
endif()

if(DEFINED NOMAD_RUNTIME_DEPENDENCIES)
    string(REPLACE "|" ";" NOMAD_RUNTIME_DEPENDENCIES "${NOMAD_RUNTIME_DEPENDENCIES}")
elseif(DEFINED NOMAD_RUNTIME AND NOT NOMAD_RUNTIME STREQUAL "")
    file(
        GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${NOMAD_RUNTIME}"
        RESOLVED_DEPENDENCIES_VAR NOMAD_RUNTIME_DEPENDENCIES
        UNRESOLVED_DEPENDENCIES_VAR NOMAD_UNRESOLVED_RUNTIME_DEPENDENCIES
        POST_EXCLUDE_REGEXES
            "^/lib/"
            "^/lib64/"
            "^/usr/lib/"
            "^/System/Library/"
    )

    if(NOMAD_UNRESOLVED_RUNTIME_DEPENDENCIES)
        list(JOIN NOMAD_UNRESOLVED_RUNTIME_DEPENDENCIES ", " NOMAD_UNRESOLVED_RUNTIME_DEPENDENCIES_TEXT)
        message(FATAL_ERROR "Unresolved Nomad runtime dependencies: ${NOMAD_UNRESOLVED_RUNTIME_DEPENDENCIES_TEXT}")
    endif()
else()
    message(FATAL_ERROR "NOMAD_RUNTIME or NOMAD_RUNTIME_DEPENDENCIES is required")
endif()

foreach(NOMAD_RUNTIME_DEPENDENCY IN LISTS NOMAD_RUNTIME_DEPENDENCIES)
    get_filename_component(NOMAD_RUNTIME_DEPENDENCY_NAME "${NOMAD_RUNTIME_DEPENDENCY}" NAME)
    set(NOMAD_RUNTIME_DEPENDENCY_DESTINATION
        "${NOMAD_RUNTIME_DESTINATION}/${NOMAD_RUNTIME_DEPENDENCY_NAME}"
    )

    if("${NOMAD_RUNTIME_DEPENDENCY}" STREQUAL "${NOMAD_RUNTIME_DEPENDENCY_DESTINATION}")
        continue()
    endif()

    file(
        COPY_FILE
        "${NOMAD_RUNTIME_DEPENDENCY}"
        "${NOMAD_RUNTIME_DEPENDENCY_DESTINATION}"
        ONLY_IF_DIFFERENT
    )
endforeach()

if(DEFINED NOMAD_RUNTIME_LICENSE_CONFIG)
    if(NOT DEFINED NOMAD_RUNTIME_VERSION OR NOT DEFINED NOMAD_RUNTIME_TARGET)
        message(FATAL_ERROR "Runtime manifest requires version and target")
    endif()
    include("${NOMAD_RUNTIME_LICENSE_CONFIG}")
    set(NOMAD_MANIFEST "{}")
    string(JSON NOMAD_MANIFEST SET "${NOMAD_MANIFEST}" schema 1)
    string(JSON NOMAD_MANIFEST SET "${NOMAD_MANIFEST}" version "\"${NOMAD_RUNTIME_VERSION}\"")
    string(JSON NOMAD_MANIFEST SET "${NOMAD_MANIFEST}" target "\"${NOMAD_RUNTIME_TARGET}\"")
    string(JSON NOMAD_MANIFEST SET "${NOMAD_MANIFEST}" files "[]")
    set(NOMAD_FILE_INDEX 0)
    function(nomadDeclareRuntimeFile PATH ROLE)
        set(ENTRY "{}")
        string(JSON ENTRY SET "${ENTRY}" path "\"${PATH}\"")
        string(JSON ENTRY SET "${ENTRY}" role "\"${ROLE}\"")
        string(JSON NOMAD_MANIFEST SET "${NOMAD_MANIFEST}" files ${NOMAD_FILE_INDEX} "${ENTRY}")
        math(EXPR NOMAD_FILE_INDEX "${NOMAD_FILE_INDEX} + 1")
        set(NOMAD_MANIFEST "${NOMAD_MANIFEST}" PARENT_SCOPE)
        set(NOMAD_FILE_INDEX "${NOMAD_FILE_INDEX}" PARENT_SCOPE)
    endfunction()
    nomadDeclareRuntimeFile("${NOMAD_RUNTIME_NAME}" runtime)
    foreach(DEPENDENCY IN LISTS NOMAD_RUNTIME_DEPENDENCIES)
        get_filename_component(NAME "${DEPENDENCY}" NAME)
        nomadDeclareRuntimeFile("${NAME}" library)
    endforeach()
    file(MAKE_DIRECTORY "${NOMAD_RUNTIME_DESTINATION}/licenses")
    foreach(LICENSE IN LISTS NOMAD_RUNTIME_LICENSES)
        string(REPLACE "|" ";" PARTS "${LICENSE}")
        list(GET PARTS 0 SOURCE)
        list(GET PARTS 1 NAME)
        file(COPY_FILE "${SOURCE}" "${NOMAD_RUNTIME_DESTINATION}/licenses/${NAME}" ONLY_IF_DIFFERENT)
        nomadDeclareRuntimeFile("licenses/${NAME}" license)
    endforeach()
    file(WRITE "${NOMAD_RUNTIME_DESTINATION}/licenses/ThirdPartyNotices.txt"
        "This product uses FreeType, copyright The FreeType Project (www.freetype.org), under the FreeType License.\n")
    nomadDeclareRuntimeFile("licenses/ThirdPartyNotices.txt" license)
    file(READ "${NOMAD_STB_IMAGE_SOURCE}" STB_IMAGE)
    string(FIND "${STB_IMAGE}" "ALTERNATIVE A - MIT License" STB_LICENSE_START)
    if(STB_LICENSE_START LESS 0)
        message(FATAL_ERROR "Could not locate stb_image license")
    endif()
    string(SUBSTRING "${STB_IMAGE}" ${STB_LICENSE_START} -1 STB_LICENSE)
    file(WRITE "${NOMAD_RUNTIME_DESTINATION}/licenses/stb_image.txt" "${STB_LICENSE}")
    nomadDeclareRuntimeFile("licenses/stb_image.txt" license)
    file(WRITE "${NOMAD_RUNTIME_DESTINATION}/runtime.json" "${NOMAD_MANIFEST}\n")
endif()
