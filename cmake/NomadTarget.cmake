function(nomad_configure_target target)
    if(NOMAD_ENABLE_SANITIZERS)
        target_link_libraries(${target} PRIVATE nomad_sanitizers)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(NOMAD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wconversion)
        if(NOMAD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(nomad_configure_library_target target)
    nomad_configure_runtime_target(${target})
    set_target_properties(${target} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    if(WIN32)
        set_target_properties(${target} PROPERTIES PREFIX "")
    endif()
    if(MINGW)
        # Explicit data exports disable MinGW's automatic function exports.
        target_link_options(${target} PRIVATE "-Wl,--export-all-symbols")
    endif()
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" BUILD_RPATH_USE_ORIGIN TRUE INSTALL_RPATH "$ORIGIN")
    endif()
endfunction()

function(nomad_configure_runtime_target target)
    nomad_configure_target(${target})
    target_compile_definitions(
        ${target}
        PRIVATE
        _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
        $<$<CONFIG:Debug>:NOMAD_DEBUG>
    )
endfunction()
