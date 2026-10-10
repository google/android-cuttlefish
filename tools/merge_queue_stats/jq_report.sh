#!/bin/bash
# Transforms locally stored GitHub GraphQL JSON files (`prs_*.json` and
# `commits_*.json`) into structured reports on merge queue health:
#
#   1. Merge Queue Attempts (`merge_queue_attempts.json`, `merge_queue_attempts.csv`):
#      Every submission of a PR to the merge queue, how long it stayed in the
#      queue (`duration_seconds` / `duration_minutes`), and whether it was
#      `"merged"` or `"rejected"` (with manual removals and queue clears counted
#      as `"rejected"`).
#
#   2. Merge Queue Rejections (`merge_queue_rejections.json`, `merge_queue_rejections.csv`,
#      `merge_queue_rejection_causes.csv`, `workflow_rejection_summary.csv`,
#      `action_rejection_summary.csv`):
#      Every rejected merge queue attempt along with the workflow(s) and check
#      run / status context / queue action that caused the rejection.
#
# Prerequisite data fetch steps:
#   1. ./pull_request_timeline_events.sh
#   2. ./fetch_merge_queue_commits.sh

set -euo pipefail

err() {
  echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2
}

if ! compgen -G "prs_*.json" >/dev/null; then
  err "ERROR: No prs_*.json found. Run ./pull_request_timeline_events.sh first."
  exit 1
fi
if ! compgen -G "commits_*.json" >/dev/null; then
  err "ERROR: No commits_*.json found. Run ./fetch_merge_queue_commits.sh first."
  exit 1
fi

# ------------------------------------------------------------------------------
# Step 1: Consolidate PR timeline pages (`prs_*.json`) sorted ascending by PR #
# ------------------------------------------------------------------------------
err "Building consolidated timeline_events.json..."
JQ_TIMELINE_EVENTS=$(cat <<'END'
[.[].data.repository.pullRequests.nodes]
  | flatten
  | sort_by(.number)
END
)
jq -s "${JQ_TIMELINE_EVENTS}" prs_*.json > timeline_events.json

# ------------------------------------------------------------------------------
# Step 2: Consolidate merge-group commit check rollups (`commits_*.json`) into
#         a fast lookup dictionary keyed by commit SHA (`{ "<oid>": { ... } }`)
# ------------------------------------------------------------------------------
err "Building consolidated commit_rollups.json..."
JQ_COMMIT_ROLLUPS=$(cat <<'END'
[
  .[].data.repository[]?
  | select(. != null and .oid != null)
  | {
      oid: .oid,
      state: (.statusCheckRollup.state // "UNKNOWN"),
      totalCount: (.statusCheckRollup.contexts.totalCount // 0),
      contexts: (.statusCheckRollup.contexts.nodes // [])
    }
]
| INDEX(.oid)
END
)
jq -s "${JQ_COMMIT_ROLLUPS}" commits_*.json > commit_rollups.json

# ------------------------------------------------------------------------------
# Step 3: Build Merge Queue Attempts report (`merge_queue_attempts.json` & `.csv`)
#
# Each completed attempt pairs an `AddedToMergeQueueEvent` with its following
# `RemovedFromMergeQueueEvent`:
#   - `status`: `"merged"` if `reason == "merged"`, else `"rejected"`
#     (including `"failed_checks"`, `"manual"`, `"queue_cleared"`,
#      `"checks_timed_out"`, `"merge_conflict"`, etc.).
#   - `duration_seconds`: `removed_at - enqueued_at` via ISO-8601 epoch conversion.
# ------------------------------------------------------------------------------
err "Generating merge_queue_attempts.json and merge_queue_attempts.csv..."
JQ_MERGE_QUEUE_ATTEMPTS=$(cat <<'END'
[
  .[]
  | . as $pr
  | [
      .timelineItems.nodes[]
      | select(
          .__typename == "AddedToMergeQueueEvent" or
          .__typename == "RemovedFromMergeQueueEvent"
        )
    ] as $events
  | [
      range(0; $events | length) as $idx
      | select($events[$idx].__typename == "AddedToMergeQueueEvent")
      | {
          added: $events[$idx],
          removed: (
            if ($idx + 1) < ($events | length) and
               $events[$idx + 1].__typename == "RemovedFromMergeQueueEvent"
            then $events[$idx + 1]
            else null
            end
          )
        }
      | select(.removed != null)
    ]
  | to_entries[]
  | (.value.added.createdAt | fromdateiso8601) as $start_ts
  | (.value.removed.createdAt | fromdateiso8601) as $end_ts
  | ($end_ts - $start_ts) as $dur_sec
  | {
      pr_number: $pr.number,
      pr_url: $pr.url,
      base_branch: $pr.baseRefName,
      attempt_number: (.key + 1),
      enqueued_at: .value.added.createdAt,
      removed_at: .value.removed.createdAt,
      duration_seconds: $dur_sec,
      duration_minutes: (($dur_sec / 60 * 100 | round) / 100),
      status: (if .value.removed.reason == "merged" then "merged" else "rejected" end),
      removal_reason: .value.removed.reason,
      enqueued_by: (.value.added.actor.login // ""),
      removed_by: (.value.removed.actor.login // ""),
      merge_group_commit: (.value.removed.beforeCommit.oid // "")
    }
]
END
)
jq "${JQ_MERGE_QUEUE_ATTEMPTS}" timeline_events.json > merge_queue_attempts.json

JQ_ATTEMPTS_CSV=$(cat <<'END'
(
  [
    "pr_number",
    "pr_url",
    "base_branch",
    "attempt_number",
    "enqueued_at",
    "removed_at",
    "duration_seconds",
    "duration_minutes",
    "status",
    "removal_reason",
    "enqueued_by",
    "removed_by",
    "merge_group_commit"
  ]
),
(
  .[]
  | [
      .pr_number,
      .pr_url,
      .base_branch,
      .attempt_number,
      .enqueued_at,
      .removed_at,
      .duration_seconds,
      .duration_minutes,
      .status,
      .removal_reason,
      .enqueued_by,
      .removed_by,
      .merge_group_commit
    ]
)
| @csv
END
)
jq -r "${JQ_ATTEMPTS_CSV}" merge_queue_attempts.json > merge_queue_attempts.csv

# ------------------------------------------------------------------------------
# Step 4: Attribute every Merge Queue Rejection to the workflow(s) / action(s)
#         that caused the rejection (`merge_queue_rejections.json`,
#         `merge_queue_rejections.csv`, and `merge_queue_rejection_causes.csv`).
#
# Causal resolution rules per rejected attempt:
#   1. Normalize every context on `merge_group_commit` (`CheckRun` -> workflow name
#      from `checkSuite.workflowRun.workflow.name` + job `name`; `StatusContext` ->
#      `"Kokoro"` for `kokoro*` contexts or `"External Status Check"` + `context`).
#   2. Exclude synthetic aggregator gate `Presubmit / presubmit-success`: in
#      `.github/workflows/presubmit.yaml`, `presubmit-success` runs with
#      `if: always()` and exits 1 whenever any upstream `Presubmit` job fails or is
#      cancelled (never executing independent tests itself).
#   3. Distinguish CI-triggered removals (`failed_checks` / `checks_timed_out`)
#      from explicit queue actions (`manual`, `queue_cleared`, `merge_conflict`):
#      - For explicit non-CI removals (`manual`, `queue_cleared`, `merge_conflict`),
#        record the queue action (`Merge Queue (manual)`, `Merge Queue (queue_cleared)`,
#        `Merge Queue (merge_conflict)`).
#      - For `failed_checks` / `checks_timed_out`:
#        * If real checks/contexts failed (`FAILURE`, `TIMED_OUT`, `ACTION_REQUIRED`,
#          `STARTUP_FAILURE`, `ERROR`), list each failing workflow & check.
#        * If `checks_timed_out` and no check failed yet, list still-running/queued
#          checks that stalled the merge group (or fallback to `checks_timed_out`).
#        * If `failed_checks` and zero checks failed on this PR's merge commit, an
#          earlier PR ahead in the merge train failed its checks and evicted this
#          downstream entry (`Upstream Batch Failure / evicted_by_earlier_queue_entry`).
# ------------------------------------------------------------------------------
err "Generating merge_queue_rejections.json, merge_queue_rejections.csv, and merge_queue_rejection_causes.csv..."
JQ_MERGE_QUEUE_REJECTIONS=$(cat <<'END'
($commit_rollups[0]) as $commits
| [
    .[]
    | select(.status == "rejected")
    | . as $att
    | ($commits[$att.merge_group_commit].contexts // []) as $raw_contexts
    | (
        [
          $raw_contexts[]
          | if .__typename == "CheckRun" then
              {
                type: "CheckRun",
                workflow: (.checkSuite.workflowRun.workflow.name // .checkSuite.app.name // "GitHub Check"),
                check_or_action: .name,
                conclusion: (.conclusion // .status),
                details_url: (.detailsUrl // .checkSuite.workflowRun.url // "")
              }
            else
              {
                type: "StatusContext",
                workflow: (if (.context | startswith("kokoro")) then "Kokoro" else "External Status Check" end),
                check_or_action: .context,
                conclusion: .state,
                details_url: (.targetUrl // "")
              }
            end
          | select((.workflow == "Presubmit" and .check_or_action == "presubmit-success") | not)
        ]
      ) as $norm_contexts
    | (
        [
          $norm_contexts[]
          | select(
              .conclusion == "FAILURE" or
              .conclusion == "TIMED_OUT" or
              .conclusion == "ACTION_REQUIRED" or
              .conclusion == "STARTUP_FAILURE" or
              .conclusion == "ERROR"
            )
        ]
      ) as $direct_failures
    | (
        if ($att.removal_reason == "manual" or $att.removal_reason == "queue_cleared" or $att.removal_reason == "merge_conflict") then
          [
            {
              type: "MergeQueue",
              workflow: "Merge Queue (\($att.removal_reason))",
              check_or_action: (
                if $att.removed_by != "" and $att.removed_by != "github-merge-queue"
                then "\($att.removal_reason) (by \($att.removed_by))"
                else $att.removal_reason
                end
              ),
              conclusion: ($att.removal_reason | ascii_upcase),
              details_url: $att.pr_url
            }
          ]
        elif ($direct_failures | length) > 0 then
          $direct_failures
        elif $att.removal_reason == "checks_timed_out" then
          (
            [
              $norm_contexts[]
              | select(
                  .conclusion == "IN_PROGRESS" or
                  .conclusion == "QUEUED" or
                  .conclusion == "PENDING" or
                  .conclusion == "WAITING" or
                  .conclusion == "REQUESTED"
                )
              | .conclusion = "TIMED_OUT_PENDING"
            ]
            | if length > 0 then . else [
                {
                  type: "MergeQueue",
                  workflow: "Merge Queue (checks_timed_out)",
                  check_or_action: "checks_timed_out",
                  conclusion: "TIMED_OUT",
                  details_url: $att.pr_url
                }
              ] end
          )
        else
          [
            {
              type: "MergeQueue",
              workflow: "Merge Queue (Upstream Batch Failure)",
              check_or_action: "evicted_by_earlier_queue_entry",
              conclusion: "CANCELLED_UPSTREAM",
              details_url: $att.pr_url
            }
          ]
        end
      ) as $causes
    | $att + {
        causes: $causes,
        causing_workflows: ([$causes[].workflow] | unique | join("; ")),
        causing_checks_or_actions: ([$causes[] | "\(.workflow): \(.check_or_action)"] | join("; ")),
        details_urls: ([$causes[].details_url | select(. != "")] | unique | join("; "))
      }
  ]
END
)
jq --slurpfile commit_rollups commit_rollups.json \
  "${JQ_MERGE_QUEUE_REJECTIONS}" merge_queue_attempts.json > merge_queue_rejections.json

# 4a. 1-row-per-rejected-attempt CSV (`merge_queue_rejections.csv`)
JQ_REJECTIONS_CSV=$(cat <<'END'
(
  [
    "pr_number",
    "pr_url",
    "attempt_number",
    "enqueued_at",
    "removed_at",
    "duration_seconds",
    "duration_minutes",
    "removal_reason",
    "removed_by",
    "merge_group_commit",
    "causing_workflows",
    "causing_checks_or_actions",
    "details_urls"
  ]
),
(
  .[]
  | [
      .pr_number,
      .pr_url,
      .attempt_number,
      .enqueued_at,
      .removed_at,
      .duration_seconds,
      .duration_minutes,
      .removal_reason,
      .removed_by,
      .merge_group_commit,
      .causing_workflows,
      .causing_checks_or_actions,
      .details_urls
    ]
)
| @csv
END
)
jq -r "${JQ_REJECTIONS_CSV}" merge_queue_rejections.json > merge_queue_rejections.csv

# 4b. 1-row-per-failure-cause CSV (`merge_queue_rejection_causes.csv`)
JQ_REJECTION_CAUSES_CSV=$(cat <<'END'
(
  [
    "pr_number",
    "pr_url",
    "attempt_number",
    "removed_at",
    "duration_minutes",
    "removal_reason",
    "workflow",
    "check_or_action",
    "conclusion",
    "details_url"
  ]
),
(
  .[] as $rej
  | $rej.causes[]
  | [
      $rej.pr_number,
      $rej.pr_url,
      $rej.attempt_number,
      $rej.removed_at,
      $rej.duration_minutes,
      $rej.removal_reason,
      .workflow,
      .check_or_action,
      .conclusion,
      .details_url
    ]
)
| @csv
END
)
jq -r "${JQ_REJECTION_CAUSES_CSV}" merge_queue_rejections.json > merge_queue_rejection_causes.csv

# ------------------------------------------------------------------------------
# Step 5: Aggregate Summary CSVs by Workflow & by Check/Action
# ------------------------------------------------------------------------------
err "Generating workflow_rejection_summary.csv and action_rejection_summary.csv..."
JQ_WORKFLOW_SUMMARY_CSV=$(cat <<'END'
(
  [
    "workflow",
    "rejections_caused",
    "total_queue_hours_lost",
    "avg_queue_minutes_per_rejection"
  ]
),
(
  [
    .[] as $rej
    | ($rej.causes | map(.workflow) | unique)[] as $wf
    | {
        workflow: $wf,
        duration_minutes: $rej.duration_minutes
      }
  ]
  | group_by(.workflow)
  | map({
      workflow: .[0].workflow,
      rejections_caused: length,
      total_queue_hours_lost: (([.[].duration_minutes] | add) / 60 * 100 | round / 100),
      avg_queue_minutes: (([.[].duration_minutes] | add) / length * 100 | round / 100)
    })
  | sort_by(-.rejections_caused)
  | .[]
  | [.workflow, .rejections_caused, .total_queue_hours_lost, .avg_queue_minutes]
)
| @csv
END
)
jq -r "${JQ_WORKFLOW_SUMMARY_CSV}" merge_queue_rejections.json > workflow_rejection_summary.csv

JQ_ACTION_SUMMARY_CSV=$(cat <<'END'
(
  [
    "workflow",
    "check_or_action",
    "rejections_caused",
    "total_queue_hours_lost",
    "avg_queue_minutes_per_rejection"
  ]
),
(
  [
    .[] as $rej
    | ($rej.causes | unique_by(.workflow + "::" + .check_or_action))[] as $c
    | {
        workflow: $c.workflow,
        check_or_action: $c.check_or_action,
        duration_minutes: $rej.duration_minutes
      }
  ]
  | group_by(.workflow + "::" + .check_or_action)
  | map({
      workflow: .[0].workflow,
      check_or_action: .[0].check_or_action,
      rejections_caused: length,
      total_queue_hours_lost: (([.[].duration_minutes] | add) / 60 * 100 | round / 100),
      avg_queue_minutes: (([.[].duration_minutes] | add) / length * 100 | round / 100)
    })
  | sort_by(-.rejections_caused)
  | .[]
  | [.workflow, .check_or_action, .rejections_caused, .total_queue_hours_lost, .avg_queue_minutes]
)
| @csv
END
)
jq -r "${JQ_ACTION_SUMMARY_CSV}" merge_queue_rejections.json > action_rejection_summary.csv

err "Done! Wrote reports:"
err "  - merge_queue_attempts.csv        ($(wc -l < merge_queue_attempts.csv) lines)"
err "  - merge_queue_rejections.csv      ($(wc -l < merge_queue_rejections.csv) lines)"
err "  - merge_queue_rejection_causes.csv ($(wc -l < merge_queue_rejection_causes.csv) lines)"
err "  - workflow_rejection_summary.csv  ($(wc -l < workflow_rejection_summary.csv) lines)"
err "  - action_rejection_summary.csv    ($(wc -l < action_rejection_summary.csv) lines)"
