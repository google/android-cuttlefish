#!/bin/bash

set -e

err() {
  echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2
}

# - https://docs.github.com/en/graphql
# - https://graphql.org/learn/queries/
# - https://docs.github.com/en/graphql/guides/using-pagination-in-the-graphql-api

page_token=\"\"

for (( page_num = 1;; page_num++ )) do
  err "Requesting page $page_num"
  query_str=$(cat <<EOF
    query {
      repository(owner:"google", name:"android-cuttlefish") {
        pullRequests(last: 100, before:${page_token}) {
          pageInfo {
            endCursor
            startCursor
            hasNextPage
            hasPreviousPage
          }
          nodes {
            url
            timelineItems(first: 100) {
              nodes {
                __typename
              }
            }
          }
        }
      }
    }
EOF
  )
  gh api graphql -f query="${query_str}" | jq > prs_$page_num.json

  page_info=.data.repository.pullRequests.pageInfo
  if [[ $(jq $page_info.hasPreviousPage prs_$page_num.json) != "true" ]]; then
    exit 0;
  fi
  page_token=$(jq $page_info.startCursor prs_$page_num.json)
done
