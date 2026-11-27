#!/usr/bin/env bash
set -euo pipefail

sdk="$1"
faust="$2"
libraries="$3"
output="$4"
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
ott="$sdk/sdk/platform/opxy-1.2.1/ott_filter"
mkdir -p "$output"
flags=(-std=c++11 -O2 -fsanitize=undefined -fno-sanitize-recover=undefined
       -I "$sdk/sdk/include" -I "$ott" -I "$repo_dir/metric-performance")

for test in test combined_test; do
    c++ "${flags[@]}" -Wall -Wextra -Werror "$repo_dir/ott-tests/$test.cpp" -o "$output/$test"
    "$output/$test"
done

"$faust/bin/faust" -lang cpp -double -cn FaustOtt -I "$libraries" \
    "$repo_dir/ott-tests/faust_reference.dsp" -o "$output/FaustOtt.hpp"
c++ "${flags[@]}" -I "$faust/include" -I "$output" \
    "$repo_dir/ott-tests/faust_reference_test.cpp" -o "$output/faust_reference_test"
"$output/faust_reference_test"

# Keep the upstream compressor math and substitute only the crossover frequency
# so the end-of-travel tests use an independent reference at the same setting.
for frequency in 1000 7000; do
    edge="$output/crossover-$frequency"
    mkdir -p "$edge"
    sed "s/F2 = 2500.0;/F2 = $frequency.0;/" "$libraries/compressors.lib" > "$edge/edge_compressors.lib"
    sed 's/co\.xfer_ott/library("edge_compressors.lib").xfer_ott/' \
        "$repo_dir/ott-tests/faust_reference.dsp" > "$edge/reference.dsp"
    "$faust/bin/faust" -lang cpp -double -cn FaustOtt -I "$edge" -I "$libraries" \
        "$edge/reference.dsp" -o "$edge/FaustOtt.hpp"
    c++ "${flags[@]}" -I "$faust/include" -I "$edge" \
        "$repo_dir/ott-tests/faust_reference_test.cpp" -o "$edge/faust_reference_test"
    position=0
    if [[ "$frequency" == 7000 ]]; then position=2147483647; fi
    "$edge/faust_reference_test" "$position"
done
