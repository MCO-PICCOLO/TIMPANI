#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2024
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

LOG_FILE="fmt_results.log"
TMP_FILE="fmt_output.txt"
mkdir -p dist/reports/fmt
REPORT_FILE="dist/reports/fmt/fmt_summary.md"

rm -f "$LOG_FILE" "$TMP_FILE" "$REPORT_FILE"

echo "Running Cargo fmt..." | tee -a "$LOG_FILE"

PROJECT_ROOT=${GITHUB_WORKSPACE:-$(pwd)}
cd "$PROJECT_ROOT/timpani_rust"

if cargo fmt --all -- --check | tee "$TMP_FILE"; then
  echo "✅ Format passed clean." | tee -a "$LOG_FILE"
  echo "✅ Format: **PASSED**" >> "$REPORT_FILE"
else
  echo "::error ::❌ Format failed!" | tee -a "$LOG_FILE"
  echo "❌ Format: **FAILED**" >> "$REPORT_FILE"
  exit 1
fi
