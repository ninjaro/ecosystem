#pragma once

#include <filesystem>

namespace ecosystem_test_support {

void write_tex_tools(const std::filesystem::path& root);
void test_tex_selection();
void test_tex_registration();
void test_tex_failures();
void test_tex_rollback();
void test_tex_aliases();
void test_tex_ownership();
void test_tex_tooling();
void test_tex_workspace();
void test_tex_native();

} // namespace ecosystem_test_support
