function(author_shaders target_name output_dir glslc)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SOURCES DEPS)
    cmake_parse_arguments(S "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT DEFINED glslc)
        message(FATAL_ERROR "Shader compiler not given")
    endif()
    set(GLCFLAGS "--target-env vulkan1.2")
    set(DIR ${CMAKE_CURRENT_SOURCE_DIR})
    if (DEFINED SHADER_BUILD_DIR)
        set(BUILD_DIR "${SHADER_BUILD_DIR}/${output_dir}")
    else()
        set(BUILD_DIR "${CMAKE_BINARY_DIR}/shaders/${output_dir}")
    endif()
    file(MAKE_DIRECTORY ${BUILD_DIR})

    foreach(SRC ${S_SOURCES})
        get_filename_component(FILE_NAME ${SRC} NAME)
        set(SPV "${BUILD_DIR}/${FILE_NAME}.spv")
        set(CMD ${glslc} --target-env vulkan1.2 ${DIR}/${SRC} -o ${SPV})
        add_custom_command(
            OUTPUT  ${SPV}
            COMMAND ${CMD}
            DEPENDS ${SRC} ${S_DEPS})
        list(APPEND SPVS ${SPV})
    endforeach(SRC)

    add_custom_target(${target_name} ALL DEPENDS ${SPVS})

    if (DEFINED SHADER_INSTALL_DIR)
        string(APPEND SHADER_INSTALL_DIR "/${output_dir}")
    else()
        set(SHADER_INSTALL_DIR ${CMAKE_INSTALL_DATADIR}/shaders/${output_dir})
    endif()
    if (NOT DEFINED SPVS)
        message(FATAL_ERROR "SPVS not defined. Can call build_shaders() before to create them. Aborting shader installation.")
    endif()
    install(FILES ${SPVS}
        DESTINATION ${SHADER_INSTALL_DIR})
endfunction()
