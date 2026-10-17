#!/usr/bin/env bash
# Builds the release preset, runs every benchmark binary with 5 repetitions,
# stores the merged Google Benchmark JSON under docs/bench-results/, and prints
# a markdown report: the machine facts, then one table per binary with the
# median and min..max over the 5 repetitions of the wall time and of every
# counter (computed with jq from the per-repetition entries).
#
# Usage: scripts/run_benchmarks.sh [--skip-build] [--repetitions N] [--filter REGEX]
#   --skip-build     reuse build/release as it is
#   --repetitions N  repetitions per benchmark (default 5)
#   --filter REGEX   passed to --benchmark_filter (default: all)
# Exit codes: 0 ok, 1 runtime failure, 2 bad flags.
set -euo pipefail

usage() {
  sed -n '9,13p' "$0" | sed 's/^# \{0,1\}//' >&2
}

skip_build=0
reps=5
filter=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) skip_build=1; shift ;;
    --repetitions)
      [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || { echo "error: --repetitions needs a positive integer" >&2; usage; exit 2; }
      reps="$2"; shift 2 ;;
    --filter)
      [[ $# -ge 2 ]] || { echo "error: --filter needs a value" >&2; usage; exit 2; }
      filter="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown flag: $1" >&2; usage; exit 2 ;;
  esac
done

command -v jq > /dev/null || { echo "error: jq is required" >&2; exit 1; }

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if [[ $skip_build -eq 0 ]]; then
  cmake --preset release > /tmp/opteng-bench-configure.log 2>&1 || { echo "error: configure failed, see /tmp/opteng-bench-configure.log" >&2; exit 1; }
  cmake --build --preset release > /tmp/opteng-bench-build.log 2>&1 || { echo "error: build failed, see /tmp/opteng-bench-build.log" >&2; exit 1; }
fi

benches=(batch_bench pricing_bench iv_bench tree_mc_bench)
for b in "${benches[@]}"; do
  [[ -x "build/release/bench/$b" ]] || { echo "error: build/release/bench/$b not found" >&2; exit 1; }
done

if [[ "$(uname -s)" == "Darwin" ]]; then
  os="macos"
  cpu="$(sysctl -n machdep.cpu.brand_string)"
  ncpu="$(sysctl -n hw.ncpu)"
  osver="macOS $(sw_vers -productVersion)"
else
  os="$(uname -s | tr '[:upper:]' '[:lower:]')"
  cpu="$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
  ncpu="$(nproc)"
  osver="$(uname -sr)"
fi
cpu_slug="$(echo "$cpu" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]\{1,\}/-/g; s/^-//; s/-$//')"
date_utc="$(date -u +%Y-%m-%d)"
stamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
compiler="$(c++ --version | head -1)"
backend="$(build/release/tools/opt_info | grep 'simd backend')"

out_dir="docs/bench-results"
mkdir -p "$out_dir"
out_json="$out_dir/${date_utc}-${os}-${cpu_slug}.json"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/opteng-bench.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT

for b in "${benches[@]}"; do
  args=(--benchmark_repetitions="$reps" --benchmark_out="$tmp/$b.json" --benchmark_out_format=json)
  [[ -n "$filter" ]] && args+=(--benchmark_filter="$filter")
  echo "running $b ..." >&2
  "build/release/bench/$b" "${args[@]}" > "$tmp/$b.txt" 2> "$tmp/$b.err" || {
    echo "error: $b failed:" >&2; cat "$tmp/$b.err" >&2; exit 1; }
done

jq -s --arg cpu "$cpu" --arg ncpu "$ncpu" --arg os "$osver" --arg compiler "$compiler" \
  --arg backend "$backend" --arg date "$stamp" '
  { machine: {cpu: $cpu, logical_cpus: $ncpu, os: $os, compiler: $compiler,
              preset: "release", simd: $backend, date: $date},
    context: .[0].context,
    benchmarks: [ .[] | .benchmarks[] ] }' \
  $(for b in "${benches[@]}"; do echo "$tmp/$b.json"; done) > "$out_json"

cat <<EOF
- CPU: $cpu, $ncpu logical CPUs
- OS: $osver
- Compiler: $compiler
- Build: \`release\` preset (-O3 -DNDEBUG, -mcpu/-march=native)
- $backend
- Date (UTC): $stamp
- Command: \`scripts/run_benchmarks.sh$( [[ $skip_build -eq 1 ]] && echo " --skip-build" )$( [[ $reps -ne 5 ]] && echo " --repetitions $reps" )$( [[ -n "$filter" ]] && echo " --filter '$filter'" )\`
- Raw JSON: \`$out_json\`
- Each cell: median of $reps repetitions, then [min .. max].
EOF

# One table per benchmark binary. Counters named time/... are seconds and are
# shown in ns; items_per_second is left out (it repeats Mopt/s).
for b in "${benches[@]}"; do
  echo
  echo "### $b"
  echo
  jq -r '
    def median: sort | if length % 2 == 1 then .[(length - 1) / 2]
                       else (.[length / 2 - 1] + .[length / 2]) / 2 end;
    # 4 significant digits; scientific notation outside [1e-3, 1e6).
    def sig: if . == 0 then "0"
             else (if . < 0 then -. else . end | log10 | floor) as $e
               | if $e >= 6 or $e < -3 then
                   ((. / pow(10; $e) * 1000 | round) / 1000 | tostring) + "e" + ($e | tostring)
                 else
                   pow(10; (if 3 - $e < 0 then 0 else 3 - $e end)) as $m
                   | ((. * $m | round) / $m | tostring)
                 end
             end;
    def cell: (median | sig) + " [" + (min | sig) + " .. " + (max | sig) + "]";
    [.benchmarks[] | select(.run_type == "iteration")] as $it
    | ($it | map(.run_name) | reduce .[] as $n ([]; if index([$n]) then . else . + [$n] end)) as $names
    | ($it | map(keys) | add | unique
        | map(select(. as $k | ["name","run_name","run_type","repetitions","repetition_index",
                               "threads","iterations","real_time","cpu_time","time_unit",
                               "family_index","per_family_instance_index","items_per_second",
                               "aggregate_name","aggregate_unit","label"] | index([$k]) | not))) as $counters
    | ($counters | map(if startswith("time/") then "ns/" + ltrimstr("time/") else . end)) as $heads
    | "| Benchmark | real time | " + ($heads | join(" | ")) + " |",
      "|---|---|" + ($heads | map("---|") | join("")),
      ($names[] as $n
        | [$it[] | select(.run_name == $n)] as $rows
        | "| `" + $n + "` | "
          + ([$rows[].real_time] | cell) + " " + $rows[0].time_unit + " | "
          + ($counters | map(. as $c
              | [$rows[] | .[$c] | select(. != null)]
              | if length == 0 then "" elif ($c | startswith("time/")) then (map(. * 1e9) | cell)
                else cell end) | join(" | "))
          + " |")
  ' "$tmp/$b.json"
done
