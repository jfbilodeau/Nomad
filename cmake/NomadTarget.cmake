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

function(nomad_configure_runtime_target target)
    nomad_configure_target(${target})
    target_compile_definitions(
        ${target}
        PRIVATE
        _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
        $<$<CONFIG:Debug>:NOMAD_DEBUG>
    )
endfunction()
