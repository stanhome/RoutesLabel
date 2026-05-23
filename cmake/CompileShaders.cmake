# CompileShaders.cmake
# 将 GLSL 源文件编译为 SPIR-V，并作为目标的构建依赖。
#
# 使用：
#   routes_compile_shaders(<target>
#       SHADERS    <glsl files...>
#       OUTPUT_DIR <output dir>
#   )
# 说明：
#   - 优先使用 glslangValidator，回退到 glslc
#   - 输出文件名为 <stem>.<stage>.spv（例如 triangle.vert.spv）
#   - 任一 shader 源变化都会触发重新编译
#   - 若 SHADERS 为空，则不创建任何自定义命令

function(routes_compile_shaders TARGET)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SHADERS" ${ARGN})

    if(NOT ARG_SHADERS)
        message(WARNING "[routes_compile_shaders] No SHADERS specified for ${TARGET}")
        return()
    endif()
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    set(_spv_outputs)
    foreach(_src IN LISTS ARG_SHADERS)
        get_filename_component(_name "${_src}" NAME)        # triangle.vert
        set(_out "${ARG_OUTPUT_DIR}/${_name}.spv")          # triangle.vert.spv

        if(GLSLANG_VALIDATOR_EXE)
            add_custom_command(
                OUTPUT  "${_out}"
                COMMAND "${GLSLANG_VALIDATOR_EXE}" -V "${_src}" -o "${_out}"
                DEPENDS "${_src}"
                COMMENT "[shader] glslangValidator -V ${_name} -> ${_name}.spv"
                VERBATIM
            )
        elseif(GLSLC_EXE)
            add_custom_command(
                OUTPUT  "${_out}"
                COMMAND "${GLSLC_EXE}" "${_src}" -o "${_out}"
                DEPENDS "${_src}"
                COMMENT "[shader] glslc ${_name} -> ${_name}.spv"
                VERBATIM
            )
        else()
            message(FATAL_ERROR "No GLSL compiler available for ${_src}")
        endif()

        list(APPEND _spv_outputs "${_out}")
    endforeach()

    add_custom_target(${TARGET}_shaders DEPENDS ${_spv_outputs})
    add_dependencies(${TARGET} ${TARGET}_shaders)
endfunction()
