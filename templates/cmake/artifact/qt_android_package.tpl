if (ECOSYSTEM_PROFILE_ANDROID)
    if (COMMAND qt_policy)
        qt_policy(SET QTP0002 NEW)
    endif ()
    set_property(TARGET {{target_name}} PROPERTY QT_ANDROID_PACKAGE_NAME {{android_package_name}})
    set_property(TARGET {{target_name}} PROPERTY QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
    set_property(TARGET {{target_name}} PROPERTY QT_ANDROID_MIN_SDK_VERSION 28)
{{android_package_source_block}}
endif ()
