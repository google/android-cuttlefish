#!/bin/bash
# Fetches merge-queue-related timeline events for all Pull Requests in
# google/android-cuttlefish using GitHub's GraphQL API.
#
# Usage:
#   ./pull_request_timeline_events.sh          # Fetches pages (resuming if interrupted)
#   ./pull_request_timeline_events.sh --clean  # Wipes prs_*.json and fetches from scratch
#   ./pull_request_timeline_events.sh --jsonl  # Also writes prs.jsonl (one PR per line)
#
# Output files:
#   prs_<page>.json - Paginated GraphQL responses (newest PRs first).
#   prs.jsonl       - With --jsonl: one compact PR node per line, deduplicated by
#                     PR number.
#
# Design notes:
#   - Filtering timelineItems by itemTypes (ADDED_TO_MERGE_QUEUE_EVENT,
#     REMOVED_FROM_MERGE_QUEUE_EVENT, MERGED_EVENT) prevents PR comments/commits
#     from truncating merge queue history while keeping query cost at 1 point/page.
#   - Uses page size 50 (~1.8s/page) so queries stay well below GitHub's 10s
#     GraphQL wall-clock timeout.
#   - Only requesting `beforeCommit { oid }` avoids slow git-object lookups per
#     event on GitHub's backend; full check/status details on merge queue commits
#     are fetched separately in batch by `fetch_merge_queue_commits.sh`.
#   - Automatically resumes from existing valid `prs_<page>.json` files unless
#     `--clean` is passed, and retries transient GitHub HTTP 502s with backoff.

set -euo pipefail

err() {
  echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2
}

rm -f prs_*.json.tmp
clean=false
jsonl=false
for arg in "$@"; do
  case "${arg}" in
    --clean) clean=true ;;
    --jsonl) jsonl=true ;;
    *) err "Usage: $0 [--clean] [--jsonl]"; exit 1 ;;
  esac
done
if [[ "${clean}" == true ]]; then
  err "Removing existing prs_*.json files (--clean specified)."
  rm -f prs_*.json
fi

page_token="null"

for (( page_num = 1;; page_num++ )); do
  page_info=.data.repository.pullRequests.pageInfo

  # If this page was already downloaded validly in an earlier interrupted run, reuse it.
  if [[ -f "prs_${page_num}.json" ]] && jq -e "${page_info}.startCursor" "prs_${page_num}.json" >/dev/null 2>&1; then
    err "Reusing existing valid prs_${page_num}.json"
  else
    err "Requesting PR page ${page_num} (before=${page_token})"
    query_str=$(cat <<EOF
      query {
        repository(owner: "google", name: "android-cuttlefish") {
          pullRequests(last: 50, before: ${page_token}) {
            pageInfo {
              endCursor
              startCursor
              hasNextPage
              hasPreviousPage
            }
            nodes {
              number
              url
              title
              baseRefName
              state
              createdAt
              mergedAt
              closedAt
              author {
                login
              }
              timelineItems(
                first: 100,
                itemTypes: [
                  ADDED_TO_MERGE_QUEUE_EVENT,
                  REMOVED_FROM_MERGE_QUEUE_EVENT,
                  MERGED_EVENT
                ]
              ) {
                totalCount
                pageInfo {
                  hasNextPage
                }
                nodes {
                  __typename
                  ... on AddedToMergeQueueEvent {
                    id
                    createdAt
                    actor {
                      login
                    }
                    enqueuer {
                      login
                    }
                    mergeQueue {
                      url
                    }
                  }
                  ... on RemovedFromMergeQueueEvent {
                    id
                    createdAt
                    reason
                    actor {
                      login
                    }
                    enqueuer {
                      login
                    }
                    mergeQueue {
                      url
                    }
                    beforeCommit {
                      oid
                    }
                  }
                  ... on MergedEvent {
                    id
                    createdAt
                    actor {
                      login
                    }
                    commit {
                      oid
                    }
                  }
                }
              }
            }
          }
        }
      }
EOF
    )

    max_attempts=5
    for (( attempt = 1; attempt <= max_attempts; attempt++ )); do
      if gh api graphql -f query="${query_str}" > "prs_${page_num}.json.tmp" 2>/dev/null \
         && jq -e "${page_info}" "prs_${page_num}.json.tmp" >/dev/null 2>&1; then
        jq . "prs_${page_num}.json.tmp" > "prs_${page_num}.json"
        rm -f "prs_${page_num}.json.tmp"
        break
      fi
      if [[ ${attempt} -eq ${max_attempts} ]]; then
        err "ERROR: Failed to fetch PR page ${page_num} after ${max_attempts} attempts."
        rm -f "prs_${page_num}.json.tmp"
        exit 1
      fi
      sleep_secs=$(( attempt * 2 ))
      err "Transient error on page ${page_num} (attempt ${attempt}/${max_attempts}), retrying in ${sleep_secs}s..."
      sleep "${sleep_secs}"
    done
  fi

  if [[ $(jq -r "${page_info}.hasPreviousPage" "prs_${page_num}.json") != "true" ]]; then
    err "Finished fetching ${page_num} pages of pull requests."
    break
  fi
  page_token=$(jq "${page_info}.startCursor" "prs_${page_num}.json")
done

if [[ "${jsonl}" == true ]]; then
  # One PR per line, nesting kept. Pages can shift while paginating, so a PR
  # may appear on two pages; keep one copy.
  jq -cs '[.[].data.repository.pullRequests.nodes[]] | unique_by(.number)[]' \
    prs_*.json > prs.jsonl.tmp
  mv prs.jsonl.tmp prs.jsonl
  err "Wrote prs.jsonl ($(wc -l < prs.jsonl | tr -d ' ') PRs)."
fi
