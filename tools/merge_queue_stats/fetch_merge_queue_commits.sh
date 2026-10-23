#!/bin/bash
# Fetches check suite / status context rollups for every unique `beforeCommit` SHA
# on `RemovedFromMergeQueueEvent` events across `prs_*.json`.
#
# Usage:
#   ./fetch_merge_queue_commits.sh          # Incrementally fetches any missing commit SHAs
#   ./fetch_merge_queue_commits.sh --clean  # Wipes commits_*.json and fetches from scratch
#
# Output files:
#   commits_<batch>.json - Batched GraphQL responses mapping `c_<oid>` to its
#                          Commit `statusCheckRollup` (`CheckRun` + `StatusContext`),
#                          plus a `patchId` added by this script (see below).
#
# Why we query `statusCheckRollup.contexts` on `RemovedFromMergeQueueEvent.beforeCommit`:
#   - On GitHub merge queues, `RemovedFromMergeQueueEvent.beforeCommit` is the
#     ephemeral merge-group commit (`gh-readonly-queue/main/pr-...`) tested by CI.
#   - GitHub Actions jobs appear as `CheckRun` nodes (with `checkSuite.workflowRun.workflow.name`
#     identifying the workflow and `name` identifying the job).
#   - External CI checks (such as Kokoro: `kokoro_*`, `cla/google`, etc.) appear as
#     `StatusContext` nodes (`context` + `state`), NOT inside `checkSuites`!
#   - Querying `statusCheckRollup.contexts(first: 100)` captures BOTH GitHub Actions
#     `CheckRun`s AND external `StatusContext` checks in one unified connection.
#   - Incrementally checks existing `commits_*.json` files and only queries missing
#     commit SHAs, making future re-runs nearly instant.
#
# Why `patchId`:
#   - A merge-group commit is the PR squashed onto whatever was ahead of it in
#     the queue, so its diff against its parent is the PR's change, already
#     rebased. `git patch-id --stable` over that diff fingerprints the change
#     independently of the base, so two attempts of the same PR with equal
#     patchIds had the same content. This lets us compare what was tested on
#     an earlier attempt with what eventually merged.

set -euo pipefail

err() {
  echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2
}

if ! compgen -G "prs_*.json" >/dev/null; then
  err "ERROR: No prs_*.json files found. Run ./pull_request_timeline_events.sh first."
  exit 1
fi

rm -f commits_*.json.tmp
if [[ "${1:-}" == "--clean" ]]; then
  err "Removing existing commits_*.json files (--clean specified)."
  rm -f commits_*.json
fi

# Collect any commit OIDs already present in local commits_*.json files.
existing_oids_file=$(mktemp)
trap 'rm -f "${existing_oids_file}"' EXIT

next_batch_num=1
if compgen -G "commits_*.json" >/dev/null; then
  jq -r '.data.repository[]?.oid // empty' commits_*.json | sort -u > "${existing_oids_file}"
  max_existing=$(ls -1 commits_*.json | sed -E 's/commits_([0-9]+)\.json/\1/' | sort -n | tail -n 1)
  next_batch_num=$(( max_existing + 1 ))
fi

# Extract all unique non-null merge-group commit SHAs from merge queue removal events
# (merged and rejected) that have not yet been fetched into commits_*.json.
mapfile -t missing_oids < <(
  comm -23 \
    <(jq -r '
        .data.repository.pullRequests.nodes[]
        | .timelineItems.nodes[]
        | select(.__typename == "RemovedFromMergeQueueEvent" and .beforeCommit.oid != null)
        | .beforeCommit.oid
      ' prs_*.json | sort -u) \
    "${existing_oids_file}"
)

existing_count=$(wc -l < "${existing_oids_file}" | tr -d ' ')
missing_count=${#missing_oids[@]}
batch_size=15
total_batches=$(( (missing_count + batch_size - 1) / batch_size ))

err "Commit rollups already cached: ${existing_count}; missing commits to fetch: ${missing_count} (${total_batches} batches of <=${batch_size})."

for (( batch_idx = 0; batch_idx < total_batches; batch_idx++ )); do
  batch_num=$(( next_batch_num + batch_idx ))
  out_file="commits_${batch_num}.json"

  start_offset=$(( batch_idx * batch_size ))
  batch_oids=( "${missing_oids[@]:${start_offset}:${batch_size}}" )

  err "Fetching commit check rollups batch ${batch_num} (${batch_idx}/${total_batches}, ${#batch_oids[@]} commits)..."

  query_str="query { repository(owner: \"google\", name: \"android-cuttlefish\") {"
  for oid in "${batch_oids[@]}"; do
    query_str+="
      c_${oid}: object(oid: \"${oid}\") {
        ... on Commit {
          oid
          statusCheckRollup {
            state
            contexts(first: 100) {
              totalCount
              nodes {
                __typename
                ... on CheckRun {
                  name
                  conclusion
                  status
                  startedAt
                  completedAt
                  detailsUrl
                  checkSuite {
                    app {
                      name
                    }
                    workflowRun {
                      url
                      event
                      workflow {
                        name
                      }
                    }
                  }
                }
                ... on StatusContext {
                  context
                  state
                  createdAt
                  targetUrl
                  description
                }
              }
            }
          }
        }
      }
    "
  done
  query_str+="} }"

  max_attempts=5
  for (( attempt = 1; attempt <= max_attempts; attempt++ )); do
    if gh api graphql -f query="${query_str}" > "${out_file}.tmp" 2>/dev/null \
       && jq -e '.data.repository' "${out_file}.tmp" >/dev/null 2>&1; then
      jq . "${out_file}.tmp" > "${out_file}"
      rm -f "${out_file}.tmp"
      break
    fi
    if [[ ${attempt} -eq ${max_attempts} ]]; then
      err "ERROR: Failed to fetch ${out_file} after ${max_attempts} attempts."
      rm -f "${out_file}.tmp"
      exit 1
    fi
    sleep_secs=$(( attempt * 2 ))
    err "Transient error on ${out_file} (attempt ${attempt}/${max_attempts}), retrying in ${sleep_secs}s..."
    sleep "${sleep_secs}"
  done
done

err "Finished fetching commit rollups (${missing_count} new commits added)."

# Add a patchId to every commit that doesn't have one yet, in place, so
# the field survives incremental runs and is also backfilled for commits
# cached before this script computed it.
diff_file=$(mktemp)
trap 'rm -f "${existing_oids_file}" "${diff_file}"' EXIT
patch_ids_added=0
for commits_file in commits_*.json; do
  [[ -f "${commits_file}" ]] || continue
  mapfile -t oids < <(
    jq -r '.data.repository[] | select(. != null) | select(has("patchId") | not) | .oid' \
      "${commits_file}"
  )
  for oid in "${oids[@]}"; do
    if gh api -H 'Accept: application/vnd.github.diff' \
        "repos/google/android-cuttlefish/commits/${oid}" > "${diff_file}" 2>&1; then
      patch_id=$(git patch-id --stable < "${diff_file}" | cut -d' ' -f1)
    elif grep -qE 'HTTP (404|406|422)' "${diff_file}"; then
      # Commit is gone or the diff is too large; nothing to retry.
      patch_id=""
    else
      err "WARNING: could not fetch diff for ${oid}; will retry next run."
      continue
    fi
    jq --arg oid "${oid}" --arg patch_id "${patch_id}" \
      '.data.repository["c_" + $oid].patchId = (if $patch_id == "" then null else $patch_id end)' \
      "${commits_file}" > "${commits_file}.tmp"
    mv "${commits_file}.tmp" "${commits_file}"
    patch_ids_added=$(( patch_ids_added + 1 ))
  done
done
err "Finished computing patch ids (${patch_ids_added} added)."
