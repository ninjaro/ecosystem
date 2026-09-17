name: Deploy

# Sphinx/Pages remains opt-in until the presentation stage is accepted.
on:
  workflow_dispatch:

permissions:
  contents: read
  issues: write
  pages: write
  pull-requests: write
  id-token: write

concurrency:
  group: pages-${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-24.04
    env:
      CMAKE_BUILD_PARALLEL_LEVEL: {{manifesto_build_parallelism}}
    steps:
      - uses: {{checkout_action}}

      - uses: {{configure_pages_action}}

      - name: Setup manifesto tool
        id: manifesto
        uses: {{manifesto_setup_action}}
        with:
          bootstrap: {{manifesto_bootstrap}}
          source-path: {{manifesto_source_path}}
          manifesto-repository: {{manifesto_repository}}
          manifesto-ref: {{manifesto_ref}}
          build-parallelism: {{manifesto_build_parallelism}}
          install-docs: 'true'
          sphinx-theme-package: {{sphinx_theme_package}}

      - name: Verify generated tracked surfaces
        uses: ./.github/actions/run-manifesto-stage
        with:
          stage-id: 01-tracked-surface
          stage-label: Tracked surface verification
          shell-command: |
            "${{ steps.manifesto.outputs.marx-binary }}" sync
            git diff --exit-code -- CMakeLists.txt .gitignore .clang-format .clang-tidy .github

      - name: Run coverage when supported
        id: coverage
        uses: ./.github/actions/run-manifesto-stage
        with:
          stage-id: 02-coverage-check
          stage-label: Coverage check
          shell-command: |
            "${{ steps.manifesto.outputs.engels-binary }}" check coverage
          skipped-exit-codes: 3

      - name: Upload coverage artifact
        if: {{github_coverage_enabled}}
        uses: {{upload_artifact_action}}
        with:
          name: coverage-{{project_id}}
          path: .ecosystem/reports/coverage.json
          if-no-files-found: warn

      - name: Generate docs site when supported
        uses: ./.github/actions/run-manifesto-stage
        with:
          stage-id: 03-docs-build
          stage-label: Documentation build
          shell-command: |
            if [ -f docs/index.md ] || [ -f docs/index.rst ]; then
              "${{ steps.manifesto.outputs.engels-binary }}" check sphinx --theme {{sphinx_theme}}
              exit $?
            fi
            mkdir -p .ecosystem/sphinx/html
            printf '%s\n' \
              '<!DOCTYPE html>' \
              '<html lang="en">' \
              '<head><meta charset="utf-8"><title>{{project_id}}</title></head>' \
              '<body><h1>{{project_id}}</h1><p>No docs/index.md or docs/index.rst surface is declared for this project yet.</p></body>' \
              '</html>' \
              > .ecosystem/sphinx/html/index.html
            exit 3
          skipped-exit-codes: 3

      - name: Upload Pages artifact
        uses: {{upload_pages_artifact_action}}
        with:
          path: .ecosystem/sphinx/html

      - name: Upload Pages reports
        uses: {{upload_artifact_action}}
        with:
          name: ci-reports-{{project_id}}-pages
          path: .ecosystem/github/reports
          if-no-files-found: warn

  deploy:
    environment:
      name: github-pages
      url: {{github_page_url}}
    runs-on: ubuntu-latest
    needs: build
    if: ${{ always() && (github.event_name == 'workflow_dispatch' || github.ref_name == github.event.repository.default_branch) }}
    outputs:
      page_url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - name: Deploy to GitHub Pages
        id: deployment
        uses: {{deploy_pages_action}}

  report:
    runs-on: ubuntu-24.04
    needs:
      - build
      - deploy
    if: ${{ always() }}
    steps:
      - uses: {{checkout_action}}

      - name: Download Pages report artifacts
        uses: {{download_artifact_action}}
        with:
          pattern: ci-reports-{{project_id}}-pages
          path: .ecosystem/github/reports
          merge-multiple: true

      - name: Summarize Pages deployment
        shell: bash
        run: |
          report_root=".ecosystem/github/reports/10-pages-deploy"
          mkdir -p "$report_root"
          status="skipped"
          if [ "${{ needs.deploy.result }}" = "success" ]; then
            status="passed"
          elif [ "${{ needs.deploy.result }}" = "failure" ] || [ "${{ needs.deploy.result }}" = "cancelled" ]; then
            status="failed"
          fi
          {
            printf '## Pages deployment\n\n'
            printf -- '- status: `%s`\n' "$status"
            printf -- '- deploy job result: `%s`\n' "${{ needs.deploy.result }}"
            if [ -n "${{ needs.deploy.outputs.page_url }}" ]; then
              printf -- '- page url: `%s`\n' "${{ needs.deploy.outputs.page_url }}"
            fi
          } > "$report_root/summary.md"
          cat "$report_root/summary.md" >> "$GITHUB_STEP_SUMMARY"

      - name: Publish Pages report
        uses: ./.github/actions/publish-manifesto-report
        with:
          project-id: {{project_id}}
          report-key: pages
          report-title: Pages
          report-directory: .ecosystem/github/reports
