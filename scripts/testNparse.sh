#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2024
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

LOG_FILE="test_results.log"
TMP_FILE="test_output.json"
mkdir -p dist/tests target
REPORT_FILE="dist/tests/test_summary.xml"

rm -f "$LOG_FILE" "$TMP_FILE" "$REPORT_FILE"

echo "Running Cargo Tests..." | tee -a "$LOG_FILE"

PROJECT_ROOT=${GITHUB_WORKSPACE:-$(pwd)}
cd "$PROJECT_ROOT/timpani_rust"

if RUSTC_BOOTSTRAP=1 cargo test --workspace -- -Z unstable-options --format json > "$TMP_FILE" 2>>"$LOG_FILE"; then
  echo "✅ Tests passed" | tee -a "$LOG_FILE"
else
  echo "::error ::❌ Tests failed!" | tee -a "$LOG_FILE"
fi

if [[ -f "$TMP_FILE" ]]; then
  if command -v jq &>/dev/null; then
    passed=$(jq -r 'select(.type == "test" and .event == "ok") | .name' "$TMP_FILE" | wc -l)
    failed=$(jq -r 'select(.type == "test" and .event == "failed") | .name' "$TMP_FILE" | wc -l)
    echo "ℹ️ Passed: $passed, Failed: $failed" | tee -a "$LOG_FILE"
  fi

  if command -v cargo2junit &>/dev/null; then
    cargo2junit < "$TMP_FILE" > "$REPORT_FILE"
  fi
fi

if grep -q "::error ::" "$LOG_FILE"; then
  exit 1
fi
echo "🎉 All tests passed!" | tee -a "$LOG_FILE"
