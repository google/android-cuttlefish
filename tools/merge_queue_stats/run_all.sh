#!/bin/bash
# End-to-end driver script to fetch fresh GitHub merge queue data, regenerate
# all CSV/JSON reports, and optionally upload updated tables & charts to Google Sheets.
#
# Usage:
#   ./run_all.sh                           # Fetches latest PR pages, incrementally fetches any new
#                                          # rejected merge-queue commit rollups, and runs jq reports.
#   ./run_all.sh --clean                   # Wipes cached JSON files (`prs_*.json`, `commits_*.json`)
#                                          # and rebuilds everything from scratch.
#   ./run_all.sh --sheets [SPREADSHEET_ID] # Uploads data & charts to Google Sheets (creates a new
#                                          # spreadsheet if SPREADSHEET_ID is omitted).

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

CLEAN_FLAG=""
UPDATE_SHEETS=false
SHEET_ID=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      CLEAN_FLAG="--clean"
      shift
      ;;
    --sheets)
      UPDATE_SHEETS=true
      if [[ $# -gt 1 && ! "$2" =~ ^-- ]]; then
        SHEET_ID="$2"
        shift
      fi
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

./pull_request_timeline_events.sh --clean
./fetch_merge_queue_commits.sh ${CLEAN_FLAG}
./jq_report.sh

if [[ "${UPDATE_SHEETS}" == "true" ]]; then
  if [[ -n "${SHEET_ID}" ]]; then
    ./upload_to_sheets.sh "${SHEET_ID}"
  else
    ./upload_to_sheets.sh
  fi
fi
