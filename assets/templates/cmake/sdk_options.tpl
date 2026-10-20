# The actor owns this local source tree; its depth is not a project path contract.
if (NOT DEFINED ECOSYSTEM_PROJECT_ROOT OR ECOSYSTEM_PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "Actor SDK configuration requires ECOSYSTEM_PROJECT_ROOT")
endif ()
option(ECOSYSTEM_PROFILE_KDE "Enable KDE-linked artifacts declared by the manifest" OFF)
option(ECOSYSTEM_PROFILE_ANDROID "Enable Android-oriented settings declared by the manifest" OFF)
