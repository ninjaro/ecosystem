option(ECOSYSTEM_BUILD_TESTS "Build declared test components" OFF)
option(ECOSYSTEM_BUILD_BENCHMARKS "Build declared benchmark components" OFF)
option(ECOSYSTEM_ENABLE_COVERAGE "Enable LLVM coverage instrumentation" OFF)
option(ECOSYSTEM_PROFILE_KDE "Enable KDE-linked artifacts declared by the manifest" OFF)
option(ECOSYSTEM_PROFILE_ANDROID "Enable Android-oriented facade settings declared by the manifest" OFF)

if (NOT DEFINED ECOSYSTEM_PROJECT_ROOT)
    get_filename_component(ECOSYSTEM_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif ()

