name: CodeQL

on:
  pull_request:
    paths:
      - 'manifest.json'
      - 'templates/**'
      - 'src/**'
      - 'include/**'
      - 'tests/**'
      - 'benchmarks/**'
      - 'core/**'
      - 'marx/**'
      - 'engels/**'
      - 'manifesto/**'
      - 'CMakeLists.txt'
      - '.gitignore'
      - '.clang-format'
      - '.clang-tidy'
      - '.github/actions/**'
      - '.github/workflows/codeql.yml'
      - 'manifesto.github.vars.json'
  push:
    paths:
      - 'manifest.json'
      - 'templates/**'
      - 'src/**'
      - 'include/**'
      - 'tests/**'
      - 'benchmarks/**'
      - 'core/**'
      - 'marx/**'
      - 'engels/**'
      - 'manifesto/**'
      - 'CMakeLists.txt'
      - '.gitignore'
      - '.clang-format'
      - '.clang-tidy'
      - '.github/actions/**'
      - '.github/workflows/codeql.yml'
      - 'manifesto.github.vars.json'
  workflow_dispatch:

permissions:
  actions: read
  contents: read
  issues: write
  pull-requests: write
  security-events: write

concurrency:
  group: codeql-${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true

jobs:
  analyze:
    runs-on: ubuntu-24.04
    env:
      CMAKE_BUILD_PARALLEL_LEVEL: {{manifesto_build_parallelism}}
    strategy:
      fail-fast: false
      matrix:
        language: ['cpp']

    steps:
      - uses: {{checkout_action}}

      - name: Setup manifesto tool
        id: manifesto
        uses: {{manifesto_setup_action}}
        with:
          bootstrap: {{manifesto_bootstrap}}
          source-path: {{manifesto_source_path}}
          manifesto-repository: {{manifesto_repository}}
          manifesto-ref: {{manifesto_ref}}
          build-parallelism: {{manifesto_build_parallelism}}

      - name: Verify generated tracked surfaces
        id: repository
        uses: ./.github/actions/run-manifesto-stage
        with:
          stage-id: 01-tracked-surface
          stage-label: Tracked surface verification
          shell-command: |
            "${{ steps.manifesto.outputs.engels-binary }}" check repo

      - uses: github/codeql-action/init@{{codeql_action_ref}}
        id: codeql_init
        continue-on-error: true
        with:
          languages: {{github_matrix_language}}
          build-mode: manual

      - name: Build project through ecosystem
        id: codeql_build
        if: ${{ steps.codeql_init.outcome == 'success' }}
        uses: ./.github/actions/run-manifesto-stage
        with:
          stage-id: 02-codeql-build
          stage-label: CodeQL build
          shell-command: |
            "${{ steps.manifesto.outputs.marx-binary }}" build debug

      - name: Run CodeQL analysis
        id: codeql_analyze
        if: ${{ steps.codeql_init.outcome == 'success' && steps.codeql_build.outputs.status == 'passed' }}
        continue-on-error: true
        uses: github/codeql-action/analyze@{{codeql_action_ref}}

      - name: Summarize CodeQL analysis
        if: ${{ always() }}
        env:
          CODEQL_INIT_STATUS: ${{ steps.codeql_init.outcome }}
          CODEQL_BUILD_STATUS: ${{ steps.codeql_build.outputs.status || 'skipped' }}
          CODEQL_ANALYZE_STATUS: ${{ steps.codeql_analyze.outcome || 'skipped' }}
        shell: bash
        run: |
          report_root=".ecosystem/github/reports/03-codeql-analysis"
          mkdir -p "$report_root"
          status="passed"
          if [ "$CODEQL_INIT_STATUS" = "failure" ] || [ "$CODEQL_BUILD_STATUS" = "failed" ] || [ "$CODEQL_ANALYZE_STATUS" = "failure" ]; then
            status="failed"
          elif [ "$CODEQL_INIT_STATUS" = "skipped" ] || [ "$CODEQL_BUILD_STATUS" = "skipped" ] || [ "$CODEQL_ANALYZE_STATUS" = "skipped" ]; then
            status="skipped"
          fi
          {
            printf '## CodeQL analysis\n\n'
            printf -- '- status: `%s`\n' "$status"
            printf -- '- init: `%s`\n' "$CODEQL_INIT_STATUS"
            printf -- '- build: `%s`\n' "$CODEQL_BUILD_STATUS"
            printf -- '- analyze: `%s`\n' "$CODEQL_ANALYZE_STATUS"
          } > "$report_root/summary.md"
          cat "$report_root/summary.md" >> "$GITHUB_STEP_SUMMARY"

      - name: Upload CodeQL reports
        if: ${{ always() }}
        continue-on-error: true
        uses: {{upload_artifact_action}}
        with:
          name: ci-reports-{{project_id}}-codeql
          path: .ecosystem/github/reports
          if-no-files-found: warn

      - name: Enforce CodeQL result
        if: ${{ always() }}
        env:
          REPOSITORY_STATUS: ${{ steps.repository.outputs.status }}
          CODEQL_INIT_STATUS: ${{ steps.codeql_init.outcome }}
          CODEQL_BUILD_STATUS: ${{ steps.codeql_build.outputs.status }}
          CODEQL_ANALYZE_STATUS: ${{ steps.codeql_analyze.outcome }}
        shell: bash
        run: |
          if [ "$REPOSITORY_STATUS" != passed ] || [ "$CODEQL_INIT_STATUS" != success ] || [ "$CODEQL_BUILD_STATUS" != passed ] || [ "$CODEQL_ANALYZE_STATUS" != success ]; then
            echo '::error::CodeQL verification failed or did not complete.'
            exit 1
          fi

  report:
    runs-on: ubuntu-24.04
    needs: analyze
    if: ${{ always() }}
    steps:
      - uses: {{checkout_action}}

      - name: Download CodeQL report artifacts
        uses: {{download_artifact_action}}
        with:
          pattern: ci-reports-{{project_id}}-codeql
          path: .ecosystem/github/reports
          merge-multiple: true

      - name: Publish CodeQL report
        uses: ./.github/actions/publish-manifesto-report
        with:
          project-id: {{project_id}}
          report-key: codeql
          report-title: CodeQL
          report-directory: .ecosystem/github/reports
