#!/usr/bin/env bash
# Snapshots the full Deribit option chain for one currency from the public
# REST API (no auth, outbound HTTPS GETs only). Each snapshot is written to a
# temp dir and renamed to OUT/<CUR>-<YYYYMMDDTHHMMSSZ>/ only when every
# request succeeded, so a failure never leaves partial or made-up files.
#
# Usage: scripts/fetch_deribit.sh [--currency BTC|ETH] [--out DIR] [--count N] [--interval SEC]
#   --currency BTC|ETH  option currency (default BTC)
#   --out DIR           output directory (default data/deribit)
#   --count N           number of snapshots (default 1)
#   --interval SEC      seconds between snapshot starts (default 300)
# Exit codes: 0 ok, 1 API/network/data failure, 2 bad flags.
#
# Files per snapshot: instruments.json (get_instruments), book_summary.json
# (get_book_summary_by_currency), index_price.json (get_index_price) and
# meta.json (fetch time, currency, URLs, script version, curl version).
set -euo pipefail
readonly kScriptVersion="1"
readonly kApi="https://www.deribit.com/api/v2/public"
usage() {
  sed -n '7,12p' "$0" | sed 's/^# \{0,1\}//' >&2
}
currency="BTC"
out="data/deribit"
count=1
interval=300
while [[ $# -gt 0 ]]; do
  case "$1" in
    --currency)
      [[ $# -ge 2 && ( "$2" == "BTC" || "$2" == "ETH" ) ]] || { echo "error: --currency must be BTC or ETH" >&2; usage; exit 2; }
      currency="$2"; shift 2 ;;
    --out)
      [[ $# -ge 2 && -n "$2" ]] || { echo "error: --out needs a directory" >&2; usage; exit 2; }
      out="$2"; shift 2 ;;
    --count)
      [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || { echo "error: --count needs a positive integer" >&2; usage; exit 2; }
      count="$2"; shift 2 ;;
    --interval)
      [[ $# -ge 2 && "$2" =~ ^[0-9]+$ ]] || { echo "error: --interval needs a non-negative integer" >&2; usage; exit 2; }
      interval="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown flag: $1" >&2; usage; exit 2 ;;
  esac
done
for tool in curl jq; do
  command -v "$tool" > /dev/null || { echo "error: $tool is required" >&2; exit 1; }
done
lower="$(printf '%s' "$currency" | tr '[:upper:]' '[:lower:]')"
url_instruments="${kApi}/get_instruments?currency=${currency}&kind=option&expired=false"
url_book="${kApi}/get_book_summary_by_currency?currency=${currency}&kind=option"
url_index="${kApi}/get_index_price?index_name=${lower}_usd"
mkdir -p "$out"
tmp=""
cleanup() {
  if [[ -n "$tmp" && -d "$tmp" ]]; then rm -rf "$tmp"; fi
}
trap cleanup EXIT
fail() {
  echo "Deribit API unreachable or returned an error; no snapshot written: $1" >&2
  exit 1
}
# fetch URL FILE JQ_CHECK: download to FILE and validate it with `jq -e`.
fetch() {
  local url="$1" file="$2" check="$3" err
  if ! err="$(curl --fail --silent --show-error --max-time 30 --retry 2 -o "$file" "$url" 2>&1)"; then
    fail "curl: ${err} (${url})"
  fi
  if ! err="$(jq -e "$check" "$file" 2>&1 > /dev/null)"; then
    fail "jq check '${check}' failed on ${url}: ${err:-false}"
  fi
}
snapshot() {
  local stamp iso dest
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  iso="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  dest="${out}/${currency}-${stamp}"
  [[ ! -e "$dest" ]] || fail "destination already exists: ${dest}"
  tmp="$(mktemp -d "${out}/.tmp-${currency}-XXXXXX")"
  fetch "$url_instruments" "${tmp}/instruments.json" '.result | length > 0'
  fetch "$url_book" "${tmp}/book_summary.json" '.result | length > 0'
  fetch "$url_index" "${tmp}/index_price.json" '.result.index_price > 0'
  jq -n --arg time "$iso" --arg cur "$currency" --arg v "$kScriptVersion" \
    --arg ui "$url_instruments" --arg ub "$url_book" --arg ux "$url_index" \
    --arg curl "$(curl --version | head -1)" \
    '{fetch_time_utc: $time, currency: $cur, script_version: $v,
      urls: {instruments: $ui, book_summary: $ub, index_price: $ux}, curl_version: $curl}' \
    > "${tmp}/meta.json"
  mv "$tmp" "$dest"
  tmp=""
  echo "wrote ${dest} ($(jq '.result | length' "${dest}/book_summary.json") book rows)"
}
for ((i = 1; i <= count; i++)); do
  start="$(date +%s)"
  snapshot
  if ((i < count)); then
    elapsed=$(( $(date +%s) - start ))
    if ((interval > elapsed)); then sleep $((interval - elapsed)); fi
  fi
done
