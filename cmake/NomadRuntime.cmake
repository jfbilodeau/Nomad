function(nomad_copy_runtime_dependencies target)
    if(NOT WIN32)
        return()
    endif()

    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND}
            -DNOMAD_RUNTIME_DEPENDENCIES_FROM_ARGUMENTS=ON
            "-DNOMAD_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:${target}>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BundleRuntimeDependencies.cmake"
            --
            $<TARGET_RUNTIME_DLLS:${target}>
            ${NOMAD_TOOLCHAIN_RUNTIME_DLLS}
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endfunction()

function(nomad_add_runtime_bundle target bundle_target)
    set(runtime_license_config "${CMAKE_CURRENT_BINARY_DIR}/RuntimeLicenses.cmake")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RuntimeLicenses.cmake.in"
        "${runtime_license_config}"
        @ONLY
    )

    set(runtime_dependency_options)
    set(runtime_dependency_arguments)
    if(WIN32)
        list(APPEND runtime_dependency_options -DNOMAD_RUNTIME_DEPENDENCIES_FROM_ARGUMENTS=ON)
        list(
            APPEND runtime_dependency_arguments
            --
            $<TARGET_RUNTIME_DLLS:${target}>
            ${NOMAD_TOOLCHAIN_RUNTIME_DLLS}
        )
    endif()

    add_custom_target(
        ${bundle_target}
        COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${target}>/runtime"
        COMMAND
            ${CMAKE_COMMAND}
            "-DNOMAD_RUNTIME=$<TARGET_FILE:${target}>"
            "-DNOMAD_RUNTIME_VERSION=${PROJECT_VERSION}"
            "-DNOMAD_RUNTIME_TARGET=${NOMAD_PACKAGE_PLATFORM}"
            "-DNOMAD_RUNTIME_LICENSE_CONFIG=${runtime_license_config}"
            "-DNOMAD_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:${target}>/runtime"
            ${runtime_dependency_options}
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BundleRuntimeDependencies.cmake"
            ${runtime_dependency_arguments}
        DEPENDS ${target}
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endfunction()
