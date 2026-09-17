if (ECOSYSTEM_BUILD_TESTS)
{{gtest_guard_open}}    {{test_source_block}}

    if ({{test_var}})
        add_executable({{test_target}}
                {{headers_var}}
                {{sources_var}}
                {{test_var_expression}}
        )
        target_include_directories({{test_target}} PUBLIC
{{include_dirs_block}}        )
{{link_block}}        target_compile_definitions({{test_target}} PRIVATE
                ECOS_TEST_SOURCE_DIR="{{source_root_expression}}"
{{runtime_definition_block}}        )
{{runtime_dependencies_block}}        ecosystem_apply_defaults({{test_target}})
{{stage_assets_block}}        add_test(NAME {{test_name}} COMMAND {{test_target}})
    endif ()
{{gtest_guard_close}}endif ()
