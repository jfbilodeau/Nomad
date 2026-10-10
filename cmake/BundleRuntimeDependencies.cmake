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

if(NOMAD_RUNTIME_DEPENDENCIES_FROM_ARGUMENTS)
    set(NOMAD_RUNTIME_DEPENDENCIES)
    set(NOMAD_READING_RUNTIME_DEPENDENCIES OFF)
    math(EXPR NOMAD_LAST_ARGUMENT_INDEX "${CMAKE_ARGC} - 1")
    foreach(NOMAD_ARGUMENT_INDEX RANGE 0 ${NOMAD_LAST_ARGUMENT_INDEX})
        if(NOMAD_READING_RUNTIME_DEPENDENCIES)
            list(APPEND NOMAD_RUNTIME_DEPENDENCIES "${CMAKE_ARGV${NOMAD_ARGUMENT_INDEX}}")
        elseif(CMAKE_ARGV${NOMAD_ARGUMENT_INDEX} STREQUAL "--")
            set(NOMAD_READING_RUNTIME_DEPENDENCIES ON)
        endif()
    endforeach()

    if(NOT NOMAD_READING_RUNTIME_DEPENDENCIES)
        message(FATAL_ERROR "Runtime dependency argument separator is missing")
    endif()
elseif(DEFINED NOMAD_RUNTIME_DEPENDENCIES)
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
    foreach(LICENSE IN LISTS NOMAD_RUNTIME_EMBEDDED_LICENSES)
        string(REPLACE "|" ";" PARTS "${LICENSE}")
        list(GET PARTS 0 SOURCE)
        list(GET PARTS 1 NAME)
        list(GET PARTS 2 START_MARKER)
        list(GET PARTS 3 END_MARKER)
        file(READ "${SOURCE}" CONTENTS)
        string(FIND "${CONTENTS}" "${START_MARKER}" LICENSE_START)
        if(LICENSE_START LESS 0)
            message(FATAL_ERROR "Could not locate ${NAME} license start in ${SOURCE}")
        endif()
        string(SUBSTRING "${CONTENTS}" ${LICENSE_START} -1 LICENSE_CONTENTS)
        string(FIND "${LICENSE_CONTENTS}" "${END_MARKER}" LICENSE_END)
        if(LICENSE_END LESS 0)
            message(FATAL_ERROR "Could not locate ${NAME} license end in ${SOURCE}")
        endif()
        string(SUBSTRING "${LICENSE_CONTENTS}" 0 ${LICENSE_END} LICENSE_CONTENTS)
        file(WRITE "${NOMAD_RUNTIME_DESTINATION}/licenses/${NAME}" "${LICENSE_CONTENTS}\n")
        nomadDeclareRuntimeFile("licenses/${NAME}" license)
    endforeach()
    file(WRITE "${NOMAD_RUNTIME_DESTINATION}/runtime.json" "${NOMAD_MANIFEST}\n")
endif()
