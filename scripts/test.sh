#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
host_cc="${CC:-clang}"
"$host_cc" -std=c11 -O2 -Iruntime tests/browser_text.c runtime/mx_browser_text.c -o build/tests/browser_text
"$host_cc" -std=c11 -O2 -Iruntime tests/browser_layout.c runtime/mx_browser_layout.c -o build/tests/browser_layout
"$host_cc" -std=c11 -O2 -Iruntime tests/browser_js.c runtime/mx_browser_js.c runtime/mx_browser_layout.c -o build/tests/browser_js
"$host_cc" -std=c11 -O2 -Iruntime tests/https_probe.c runtime/mx_http.c runtime/mx_browser_text.c -o build/tests/https_probe
for test in browser_text browser_layout browser_js https_probe; do
    "build/tests/$test"
done
echo "All native checks passed"
