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
