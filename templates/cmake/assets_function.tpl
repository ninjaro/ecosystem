function(ecosystem_stage_assets target_name)
    file(GENERATE
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/manifesto_assets_${target_name}_$<CONFIG>.cmake"
            CONTENT [==[{{assets_stage_script}}]==]
    )
    add_custom_command(
            TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
            "-Dasset_source={{source_root_expression}}/assets"
            "-Dasset_destination=$<TARGET_FILE_DIR:${target_name}>/assets"
            -P "${CMAKE_CURRENT_BINARY_DIR}/manifesto_assets_${target_name}_$<CONFIG>.cmake"
            VERBATIM
    )
endfunction()
{{assets_embed_function_block}}
{{assets_install_block}}
