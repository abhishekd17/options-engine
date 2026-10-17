#!/usr/bin/env bash
# Makes a small committed copy of a Deribit snapshot written by
# fetch_deribit.sh: keeps only the fields the loader reads and, optionally,
# only the listed expiries. It never changes a value and keeps the directory
# name (DST is the parent directory of the copy).
#
# Usage: scripts/trim_snapshot.sh SRC DST [--expiries 30OCT26,27NOV26]
#   SRC          snapshot directory (<CUR>-<YYYYMMDDTHHMMSSZ>)
#   DST          parent directory for the trimmed copy
#   --expiries   comma-separated expiry tokens (the 2nd field of the instrument name)
# Exit codes: 0 ok, 1 jq/file failure, 2 bad usage.
set -euo pipefail
usage() {
  sed -n '7,11p' "$0" | sed 's/^# \{0,1\}//' >&2
}
[[ $# -ge 2 ]] || { echo "error: SRC and DST are required" >&2; usage; exit 2; }
src="$1"
dst="$2"
shift 2
expiries=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --expiries)
      [[ $# -ge 2 && "$2" =~ ^[0-9A-Z]+(,[0-9A-Z]+)*$ ]] || { echo "error: --expiries needs a list like 30OCT26,27NOV26" >&2; usage; exit 2; }
      expiries="$2"; shift 2 ;;
    *) echo "error: unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done
command -v jq > /dev/null || { echo "error: jq is required" >&2; exit 1; }
for f in instruments.json book_summary.json index_price.json meta.json; do
  [[ -f "${src}/${f}" ]] || { echo "error: missing ${src}/${f}" >&2; exit 1; }
done
out="${dst}/$(basename "$src")"
mkdir -p "$out"
# keep(name): true when no filter is set or the name's expiry token is listed.
filter='def keep: ($exp == "") or ((split("-")[1]) as $e | ($exp | split(",") | index($e)) != null);'
jq -c --arg exp "$expiries" "$filter"'
  {result: [.result[] | select(.instrument_name | keep)
            | {instrument_name, bid_price, ask_price, mark_price, mark_iv, underlying_price,
               underlying_index, interest_rate, creation_timestamp, open_interest}],
   usIn, usOut}' "${src}/book_summary.json" > "${out}/book_summary.json"
jq -c --arg exp "$expiries" "$filter"'
  {result: [.result[] | select(.instrument_name | keep)
            | {instrument_name, kind, option_type, strike, expiration_timestamp,
               instrument_type, settlement_currency}],
   usIn, usOut}' "${src}/instruments.json" > "${out}/instruments.json"
cp "${src}/index_price.json" "${src}/meta.json" "$out/"
echo "wrote ${out} ($(jq '.result | length' "${out}/book_summary.json") book rows, $(du -sk "$out" | cut -f1) KB)"
