#!/bin/bash

set -e

err() {
  echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2
}

# https://jqlang.org/manual/

JQ_TIMELINE_EVENTS=$(cat <<'END'
[.[].data.repository.pullRequests.nodes]
  | flatten
  | sort_by(.url|split("/")|last|tonumber)
END
)

jq -s "${JQ_TIMELINE_EVENTS}" prs_*.json > timeline_events.json

JQ_COUNT_EVENTS=$(cat <<'END'
map({
  url: .url,
  events: (
    .timelineItems.nodes
      | group_by(.__typename)
      | map({(.[0].__typename): .|length})
      | add)
})
END
)
jq "${JQ_COUNT_EVENTS}" timeline_events.json > count_events.json

JQ_ONLY_MERGE_QUEUE=$(cat <<'END'
.[]
  | select(.events|has("RemovedFromMergeQueueEvent"))
  | select(.events|(has("MergedEvent")))
END
)
jq "${JQ_ONLY_MERGE_QUEUE}" count_events.json > only_merge_queue.json

JQ_OUTPUT=$(cat <<'END'
[.url,.events.RemovedFromMergeQueueEvent]
  | @csv
END
)
jq "${JQ_OUTPUT}" only_merge_queue.json > output.csv

err "Wrote output to output.csv"
