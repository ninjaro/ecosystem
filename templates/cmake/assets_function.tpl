function(ecosystem_stage_assets target_name)
    add_custom_command(
            TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            {{source_root_expression}}/assets
            $<TARGET_FILE_DIR:${target_name}>/assets
    )
endfunction()
{{assets_embed_function_block}}
{{assets_install_block}}
