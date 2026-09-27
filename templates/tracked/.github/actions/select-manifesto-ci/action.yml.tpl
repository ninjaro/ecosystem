name: Select Manifesto CI
description: Select local verification scope from the GitHub event without running project tools.

outputs:
  scope:
    description: cheap, full, security, or skip.
    value: ${{ steps.select.outputs.scope }}

runs:
  using: composite
  steps:
    - name: Select verification scope
      id: select
      shell: bash
      env:
        EVENT_NAME: ${{ github.event_name }}
        CURRENT_REF: ${{ github.ref }}
        DEFAULT_BRANCH: ${{ github.event.repository.default_branch }}
        PR_BASE_REF: ${{ github.event.pull_request.base.ref }}
      run: |
        scope=skip
        case "$EVENT_NAME" in
          push|pull_request|schedule)
            if [ -z "$DEFAULT_BRANCH" ]; then
              echo '::error::Cannot select verification without the repository default branch.'
              exit 1
            fi
            ;;
        esac
        case "$EVENT_NAME" in
          push)
            if [ "$CURRENT_REF" = "refs/heads/$DEFAULT_BRANCH" ]; then
              scope=full
            elif [[ "$CURRENT_REF" == refs/heads/* ]]; then
              scope=cheap
            fi
            ;;
          pull_request)
            if [ -z "$PR_BASE_REF" ]; then
              echo '::error::Cannot select PR verification without its base branch.'
              exit 1
            fi
            scope=cheap
            if [ "$PR_BASE_REF" = "$DEFAULT_BRANCH" ]; then
              scope=full
            fi
            ;;
          workflow_dispatch) scope=full ;;
          schedule) scope=security ;;
        esac
        echo "scope=$scope" >> "$GITHUB_OUTPUT"
        printf 'Verification scope: %s\n' "$scope" >> "$GITHUB_STEP_SUMMARY"
