if (OpenCV_FOUND)
    target_compile_definitions({{target_name}} {{link_scope}} {{project_prefix}}_OPENCV)
endif ()
