function(ecosystem_apply_defaults target_name)
    get_target_property(target_type ${target_name} TYPE)
    set(target_scope PRIVATE)
    if (target_type STREQUAL "INTERFACE_LIBRARY")
        set(target_scope INTERFACE)
    endif ()

    if (MSVC)
        target_compile_options(${target_name} ${target_scope} /W4 /WX)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name}
                ${target_scope}
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Werror
                "$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>"
        )
    endif ()

    if (ECOSYSTEM_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name}
                ${target_scope}
                -fprofile-instr-generate
                -fcoverage-mapping
        )
        target_link_options(${target_name}
                ${target_scope}
                -fprofile-instr-generate
                -fcoverage-mapping
        )
    endif ()
endfunction()
