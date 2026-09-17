name: Checks

# Policy and artifact selection belong to the same local command developers run.
# No path filter: a manifest may own sources under any project directory.
on:
  pull_request:
  push:
  workflow_dispatch:

permissions:
  contents: read
  issues: write
  pull-requests: write

concurrency:
  group: checks-${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true

jobs:
  checks:
    runs-on: ubuntu-24.04
    env:
      CMAKE_BUILD_PARALLEL_LEVEL: {{manifesto_build_parallelism}}
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

      - name: Run required local checks
        id: required
        uses: ./.github/actions/run-manifesto-stage
        with:
          stage-id: 01-required-checks
          stage-label: Required local checks
          shell-command: |
            "${{ steps.manifesto.outputs.engels-binary }}" check ci
          skipped-exit-codes: ''

      - name: Upload check reports
        if: ${{ always() }}
        continue-on-error: true
        uses: {{upload_artifact_action}}
        with:
          name: ci-reports-{{project_id}}-checks
          path: |
            .ecosystem/github/reports
            .ecosystem/reports
          if-no-files-found: warn

      - name: Publish check report
        if: ${{ always() }}
        continue-on-error: true
        uses: ./.github/actions/publish-manifesto-report
        with:
          project-id: {{project_id}}
          report-key: checks
          report-title: Checks
          report-directory: .ecosystem/github/reports

      - name: Enforce required result
        if: ${{ always() }}
        env:
          CHECK_OUTCOME: ${{ steps.required.outcome }}
          CHECK_STATUS: ${{ steps.required.outputs.status }}
          CHECK_EXIT_CODE: ${{ steps.required.outputs.exit-code }}
        shell: bash
        run: |
          if [ "$CHECK_OUTCOME" != success ] || [ "$CHECK_STATUS" != passed ] || [ "$CHECK_EXIT_CODE" != 0 ] || [ ! -s .ecosystem/github/reports/01-required-checks/summary.md ] || [ ! -f .ecosystem/github/reports/01-required-checks/output.log ]; then
            echo '::error::Required local checks failed or did not produce a complete result. See the stage report and setup logs.'
            exit 1
          fi
