{{assets_inventory_functions}}
function(ecosystem_stage_assets target_name)
    file(GENERATE
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ecosystem_assets_${target_name}_$<CONFIG>.cmake"
            CONTENT [==[{{assets_stage_script}}]==]
    )
    # A new staging policy must take effect even if application sources did not
    # change. Native Make/Ninja link rules depend on the generated script.
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_BINARY_DIR}/ecosystem_assets_${target_name}_$<CONFIG>.cmake")
    add_custom_command(
            TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
            "-Dasset_source={{source_root_expression}}/assets"
            "-Dasset_destination=$<TARGET_FILE_DIR:${target_name}>/assets"
            -P "${CMAKE_CURRENT_BINARY_DIR}/ecosystem_assets_${target_name}_$<CONFIG>.cmake"
            VERBATIM
    )
endfunction()
{{assets_embed_function_block}}
{{assets_install_block}}
