#include "packages/package_surface_rule.hpp"

#include <string_view>
#include <vector>

namespace ecosystem {
namespace package_surface_rule_support {

    struct package_surface_rule_entry {
        package_surface_block block;
        package_surface_rule rule;
    };

    package_surface_target_rule make_surface_target_rule(
        std::string_view name,
        const std::vector<std::string_view>& link_libraries = {},
        const std::vector<std::string_view>& include_directories = {},
        std::string_view condition = {},
        const bool system_include_directories = false
    ) {
        package_surface_target_rule target;
        target.name = name;
        target.link_libraries = link_libraries;
        target.include_directories = include_directories;
        target.condition = condition;
        target.system_include_directories = system_include_directories;
        return target;
    }

    package_surface_rule make_static_surface_rule(
        const std::vector<std::string_view>& find_package_lines,
        const std::vector<std::string_view>& pre_target_lines = {},
        const package_surface_target_rule& support_target = {},
        const bool blank_line_before_target = false
    ) {
        package_surface_rule rule;
        rule.source = package_surface_source::static_rule;
        rule.find_package_lines = find_package_lines;
        rule.pre_target_lines = pre_target_lines;
        rule.support_target = support_target;
        rule.blank_line_before_target = blank_line_before_target;
        return rule;
    }

    package_surface_rule make_component_surface_rule(
        const dependency_id dependency,
        const std::vector<std::string_view>& pre_find_package_lines,
        std::string_view find_package_open_line,
        const std::vector<std::string_view>& post_find_package_lines = {},
        const std::vector<std::string_view>& qt_automation_lines = {},
        const bool blank_line_before_qt_automation = false
    ) {
        package_surface_rule rule;
        rule.source = package_surface_source::component_rule;
        rule.component_rule.dependency = dependency;
        rule.component_rule.pre_find_package_lines = pre_find_package_lines;
        rule.component_rule.find_package_open_line = find_package_open_line;
        rule.component_rule.post_find_package_lines = post_find_package_lines;
        rule.component_rule.qt_automation_lines = qt_automation_lines;
        rule.component_rule.blank_line_before_qt_automation
            = blank_line_before_qt_automation;
        return rule;
    }

    package_surface_rule make_kde_surface_rule() {
        package_surface_rule rule;
        rule.source = package_surface_source::profile_rule;
        rule.profile_rule.option_lines = {
            "option(ECOSYSTEM_PROFILE_KDE \"Enable KDE probe requirements\" "
            "OFF)",
        };
        rule.profile_rule.condition = "ECOSYSTEM_PROFILE_KDE";
        rule.profile_rule.component_dependency = dependency_id::kde;
        rule.profile_rule.component_find_package_line_prefix
            = "    find_package(KF6";
        rule.profile_rule.component_find_package_line_suffix
            = " CONFIG REQUIRED)";
        rule.profile_rule.component_find_package_value_aliases = {
            { "KIOCore", "KIO" },
            { "KIOFileWidgets", "KIO" },
            { "KIOGui", "KIO" },
            { "KIOWidgets", "KIO" },
        };
        rule.profile_rule.fixed_find_package_lines = {
            {
                dependency_id::kdegames,
                "    find_package(KDEGames6 REQUIRED)",
            },
        };
        rule.profile_rule.support_target_name = "ecosystem_kde_support";
        rule.profile_rule.support_component_dependency = dependency_id::kde;
        rule.profile_rule.support_component_line_prefix = "            ";
        rule.profile_rule.support_component_value_prefix = "KF6::";
        rule.profile_rule.fixed_support_link_lines = {
            {
                dependency_id::kdegames,
                "            KDEGames6",
            },
        };
        return rule;
    }

    const std::vector<package_surface_rule_entry>&
    package_surface_rule_entries() {
        static const std::vector<package_surface_rule_entry> entries {
            {
                package_surface_block::nlohmann_json,
                make_static_surface_rule({
                    "if (ANDROID)",
                    ("    find_package(nlohmann_json 3.12 CONFIG REQUIRED "
                     "NO_CMAKE_FIND_ROOT_PATH)"),
                    ("    find_path(ECOSYSTEM_NLOHMANN_JSON_INCLUDE_DIR "
                     "nlohmann/json.hpp REQUIRED NO_CMAKE_FIND_ROOT_PATH)"),
                    ("    set(ECOSYSTEM_NLOHMANN_JSON_STAGE_DIR "
                     "\"${CMAKE_BINARY_DIR}/ecosystem-host-headers/"
                     "nlohmann_json\")"),
                    ("    file(MAKE_DIRECTORY "
                     "\"${ECOSYSTEM_NLOHMANN_JSON_STAGE_DIR}\")"),
                    ("    file(COPY "
                     "\"${ECOSYSTEM_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann\" "
                     "DESTINATION "
                     "\"${ECOSYSTEM_NLOHMANN_JSON_STAGE_DIR}\")"),
                    ("    target_include_directories("
                     "nlohmann_json::nlohmann_json INTERFACE "
                     "\"${ECOSYSTEM_NLOHMANN_JSON_STAGE_DIR}\")"),
                    "else ()",
                    "    find_package(nlohmann_json 3.12 CONFIG REQUIRED)",
                    "endif ()",
                }),
            },
            {
                package_surface_block::openssl,
                make_component_surface_rule(
                    dependency_id::openssl, {},
                    "find_package(OpenSSL 3 REQUIRED COMPONENTS",
                    {
                        "set(ECOSYSTEM_OPENSSL_CRYPTO_LIBRARY "
                        "\"${OPENSSL_CRYPTO_LIBRARY}\" CACHE INTERNAL "
                        "\"Resolved OpenSSL Crypto libraries\" FORCE)",
                        "set(ECOSYSTEM_OPENSSL_SSL_LIBRARY "
                        "\"${OPENSSL_SSL_LIBRARY}\" CACHE INTERNAL \"Resolved "
                        "OpenSSL SSL libraries\" FORCE)",
                    }
                ),
            },
            {
                package_surface_block::llvm_clang,
                make_static_surface_rule(
                    {
                        "find_package(LLVM CONFIG REQUIRED)",
                        "find_package(Clang CONFIG REQUIRED)",
                    },
                    {
                        "add_definitions(${LLVM_DEFINITIONS})",
                    },
                    make_surface_target_rule(
                        "ecosystem_llvm_clang_support", { "libclang" },
                        { "${LLVM_INCLUDE_DIRS}", "${CLANG_INCLUDE_DIRS}" }, {},
                        true
                    ),
                    true
                ),
            },
            {
                package_surface_block::qt,
                make_component_surface_rule(
                    dependency_id::qt,
                    {
                        "set(QT_MAJOR_VERSION 6)",
                        "set(QT_DEFAULT_MAJOR_VERSION 6)",
                    },
                    "find_package(Qt6 {{qt_version}} CONFIG REQUIRED "
                    "COMPONENTS",
                    {},
                    {
                        "set(CMAKE_AUTOMOC ON)",
                        "set(CMAKE_AUTORCC ON)",
                        "set(CMAKE_AUTOUIC ON)",
                    },
                    true
                ),
            },
            {
                package_surface_block::kde,
                make_kde_surface_rule(),
            },
            {
                package_surface_block::opencv,
                make_static_surface_rule(
                    { "find_package(OpenCV QUIET)" }, {},
                    make_surface_target_rule(
                        "ecosystem_optional_opencv", { "${OpenCV_LIBS}" },
                        { "${OpenCV_INCLUDE_DIRS}" }, "OpenCV_FOUND"
                    )
                ),
            },
            {
                package_surface_block::eigen,
                make_static_surface_rule(
                    { "find_package(Eigen3 3.3 QUIET)" }, {},
                    make_surface_target_rule(
                        "ecosystem_optional_eigen", { "Eigen3::Eigen" }, {},
                        "Eigen3_FOUND"
                    )
                ),
            },
            {
                package_surface_block::jni,
                make_static_surface_rule(
                    { "find_package(JNI REQUIRED)" }, {},
                    make_surface_target_rule(
                        "ecosystem_jni_support", { "${JNI_LIBRARIES}" },
                        { "${JNI_INCLUDE_DIRS}" }
                    )
                ),
            },
            {
                package_surface_block::cxxopts,
                make_static_surface_rule({
                    "if (ANDROID)",
                    ("    find_package(cxxopts CONFIG REQUIRED "
                     "NO_CMAKE_FIND_ROOT_PATH)"),
                    ("    find_path(ECOSYSTEM_CXXOPTS_INCLUDE_DIR "
                     "cxxopts.hpp REQUIRED NO_CMAKE_FIND_ROOT_PATH)"),
                    ("    set(ECOSYSTEM_CXXOPTS_STAGE_DIR "
                     "\"${CMAKE_BINARY_DIR}/ecosystem-host-headers/"
                     "cxxopts\")"),
                    ("    file(MAKE_DIRECTORY "
                     "\"${ECOSYSTEM_CXXOPTS_STAGE_DIR}\")"),
                    ("    file(COPY "
                     "\"${ECOSYSTEM_CXXOPTS_INCLUDE_DIR}/cxxopts.hpp\" "
                     "DESTINATION \"${ECOSYSTEM_CXXOPTS_STAGE_DIR}\")"),
                    ("    target_include_directories(cxxopts::cxxopts "
                     "INTERFACE \"${ECOSYSTEM_CXXOPTS_STAGE_DIR}\")"),
                    "else ()",
                    "    find_package(cxxopts CONFIG REQUIRED)",
                    "endif ()",
                }),
            },
            {
                package_surface_block::zlib,
                make_static_surface_rule({ "find_package(ZLIB REQUIRED)" }),
            },
            {
                package_surface_block::gtest,
                make_static_surface_rule({ "find_package(GTest QUIET)" }),
            },
            {
                package_surface_block::benchmark,
                make_static_surface_rule({ "find_package(benchmark QUIET)" }),
            },
        };
        return entries;
    }

    const package_surface_rule_entry*
    find_package_surface_rule_entry(const package_surface_block block) {
        for (const package_surface_rule_entry& entry :
             package_surface_rule_entries()) {
            if (entry.block == block) {
                return &entry;
            }
        }
        return nullptr;
    }

} // namespace package_surface_rule_support

using namespace package_surface_rule_support;

const package_surface_rule*
find_package_surface_rule(const package_surface_block block) {
    const package_surface_rule_entry* entry
        = find_package_surface_rule_entry(block);
    if (entry == nullptr) {
        return nullptr;
    }
    return &entry->rule;
}

} // namespace ecosystem
