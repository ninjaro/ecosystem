#include "../documentation.hpp"
#include "../test_support.hpp"
#include "workspace/assets.hpp"

namespace ecosystem_test_support {

void test_native_asset_pages(const fs::path& project) {
    const auto media = ecosystem::capture_command_result(
        { "python3",
          (fs::path(ECOS_TEST_SOURCE_DIR)
           / "assets/dataset/test/suite/presentation/media.py")
              .string(),
          (project / "assets/showcase").string() },
        project
    );
    require_true(media.exit_code == 0, media.output);
    std::string error;
    for (const auto& entry : std::vector<ecosystem::asset_entry> {
             { "still", "frame.png", "image", "Still & notes", "2026" },
             { "clip", "demo.webm", "video", "Clip", "2025" },
             { "brief", "brief.pdf", "pdf", "Brief", "2024" },
             { "mp4", "demo.mp4", "video", "MP4 clip", "2023" } })
        require_true(
            ecosystem::put_asset_entry(project, "showcase", entry, {}, &error),
            error
        );
    write_text(
        project / "assets/showcase/readme.md",
        "# Curated media\n\n**Before** [Open frame][frame].\n\n"
        "<carousel/>\n\n**After**.\n\n[frame]: frame.png\n"
    );
    write_text(project / "assets/sets/index.html", "Selected runtime page\n");
    require_true(
        ecosystem::put_asset_entry(
            project, "sets",
            { "page", "index.html", "data", "Runtime page", {} }, {}, &error
        ),
        error
    );
    const auto index = read_text(project / "assets/showcase/index.tsv");
    const auto readme = read_text(project / "assets/showcase/readme.md");
    const auto pdf = read_text(project / "assets/showcase/brief.pdf");
    auto result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(result.exit_code == 0, result.output);
    const auto site = ecosystem::local_sphinx_dir(project) / "html";
    const auto page
        = read_text(site / "_ecosystem/catalog/showcase/index.html");
    require_contains(
        page, "<strong>Before</strong>", "paired Markdown must render with MyST"
    );
    require_contains(
        page, "href=\"../../showcase/frame.png\">Open frame</a>",
        "Markdown references spanning the marker must resolve to selected media"
    );
    require_true(
        page.find("Before") < page.find("carousel-controls"),
        "paired prose must precede the carousel"
    );
    require_true(
        page.find("carousel-track") < page.find("<strong>After</strong>"),
        "paired prose must follow the carousel"
    );
    require_contains(
        page, "<video controls", "native site must contain a playable video"
    );
    require_contains(
        page, "poster=\"../../previews/",
        "videos must have native frame previews"
    );
    require_contains(
        page, "alt=\"First page: Brief\"", "PDFs must have a first-page preview"
    );
    std::size_t previews = 0;
    for (const auto& entry :
         fs::directory_iterator(site / "_ecosystem/previews")) {
        const auto contents = read_text(entry.path());
        require_true(
            contents.starts_with("\x89PNG\r\n\x1a\n")
                && contents.size() < 8 * 1024 * 1024,
            "native previews must be bounded PNGs"
        );
        ++previews;
    }
    require_true(
        previews == 3,
        "only the selected PDF, WebM and MP4 need derived previews"
    );
    require_contains(page, "Still &amp; notes", "captions must escape HTML");
    require_true(
        fs::is_regular_file(site / "_ecosystem/carousel.js")
            && fs::is_regular_file(site / "_ecosystem/carousel.css"),
        "controls belong to the site"
    );
    require_true(
        read_text(site / "_ecosystem/assets/sets/index.html")
                == "Selected runtime page\n"
            && fs::is_regular_file(site / "_ecosystem/catalog/sets/index.html"),
        "selected data and generated navigation must have distinct ownership"
    );
    require_true(
        read_text(project / "assets/showcase/index.tsv") == index
            && read_text(project / "assets/showcase/readme.md") == readme,
        "presentation must preserve index and readme ownership"
    );
    write_text(
        project / "assets/showcase/brief.pdf", "malformed PDF fixture\n"
    );
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 5
            && read_text(site / "_ecosystem/catalog/showcase/index.html")
                == page,
        "conversion failures must preserve the published media and pages"
    );
    require_contains(
        result.output, "preview failed for assets/showcase/brief.pdf",
        "native conversion failures must identify the selected input"
    );
    write_text(project / "assets/showcase/brief.pdf", pdf);
    for (const auto* invalid :
         { "No carousel marker\n", "```\n<carousel/>\n```\n" }) {
        write_text(project / "assets/showcase/readme.md", invalid);
        result = run_engels_cli(project, "check sphinx --theme alabaster");
        require_true(
            result.exit_code == 5
                && read_text(site / "_ecosystem/catalog/showcase/index.html")
                    == page,
            "invalid paired metadata must preserve the whole published site"
        );
    }
    write_text(project / "assets/showcase/readme.md", readme);
    fs::remove_all(project / "assets/showcase");
    fs::remove_all(project / "assets/sets");
    result = run_engels_cli(project, "check sphinx --theme alabaster");
    require_true(
        result.exit_code == 0
            && !fs::exists(site / "_ecosystem/catalog/showcase/index.html")
            && !fs::exists(site / "_ecosystem/carousel.js")
            && !fs::exists(site / "_ecosystem/previews"),
        "a fresh build must retire removed category pages and controls"
    );
}

} // namespace ecosystem_test_support
