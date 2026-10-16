configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/RuntimeLicenses.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RuntimeLicenses.cmake"
    @ONLY
)

foreach(MODULE IN ITEMS nomad-system nomad-game)
    set(MODULE_DESTINATION "$<TARGET_FILE_DIR:nomad-runtime>/modules/${MODULE}")
    set(MODULE_DEPENDENCIES)
    if(WIN32)
        list(APPEND MODULE_DEPENDENCIES
            -DNOMAD_RUNTIME_DEPENDENCIES_FROM_ARGUMENTS=ON
        )
        set(MODULE_ARGUMENTS -- $<TARGET_RUNTIME_DLLS:${MODULE}> ${NOMAD_TOOLCHAIN_RUNTIME_DLLS})
    else()
        set(MODULE_ARGUMENTS)
    endif()
    add_custom_target(${MODULE}-bundle
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MODULE_DESTINATION}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/modules/${MODULE}/module.toml"
            "${MODULE_DESTINATION}/module.toml"
        COMMAND ${CMAKE_COMMAND}
            "-DNOMAD_RUNTIME=$<TARGET_FILE:${MODULE}>"
            "-DNOMAD_RUNTIME_DESTINATION=${MODULE_DESTINATION}"
            "-DNOMAD_RUNTIME_VERSION=${PROJECT_VERSION}"
            "-DNOMAD_RUNTIME_TARGET=${NOMAD_PACKAGE_PLATFORM}"
            "-DNOMAD_RUNTIME_LICENSE_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/RuntimeLicenses.cmake"
            -DNOMAD_RUNTIME_ROLE=library
            -DNOMAD_RUNTIME_IS_LIBRARY=ON
            ${MODULE_DEPENDENCIES}
            -P "${CMAKE_CURRENT_LIST_DIR}/BundleRuntimeDependencies.cmake"
            ${MODULE_ARGUMENTS}
        DEPENDS ${MODULE}
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endforeach()

if(TARGET nomad_test)
    add_custom_target(nomad-test-modules
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:nomad-runtime>/modules"
            "$<TARGET_FILE_DIR:nomad_test>/modules"
        DEPENDS nomad-modules
        VERBATIM
    )
    if(TARGET nomad-runtime-bundle)
        add_dependencies(nomad-test-modules nomad-runtime-bundle)
        add_custom_command(TARGET nomad-test-modules POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:nomad-runtime>/runtime"
                "$<TARGET_FILE_DIR:nomad_test>/runtime"
            VERBATIM
        )
    endif()
    add_dependencies(nomad_test nomad-test-modules)
endif()
add_custom_target(nomad-modules DEPENDS nomad-system-bundle nomad-game-bundle)
foreach(PROGRAM IN ITEMS nomad-runtime nomad-cli nomadc nomad-lsp nomad_test)
    if(TARGET ${PROGRAM})
        add_dependencies(${PROGRAM} nomad-modules)
    endif()
endforeach()
