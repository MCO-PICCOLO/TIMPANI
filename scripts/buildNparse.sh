#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2024
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

LOG_FILE="build_results.log"
TMP_FILE="build_output.txt"
rm -f "$LOG_FILE" "$TMP_FILE"

echo "Running Cargo Build..." | tee -a "$LOG_FILE"

PROJECT_ROOT=${GITHUB_WORKSPACE:-$(pwd)}
git config --global --add safe.directory "$PROJECT_ROOT" || true
cd "$PROJECT_ROOT/timpani_rust"

echo "Building entire workspace..." | tee -a "$LOG_FILE"
if cargo build -vv --workspace | tee "$TMP_FILE"; then
  echo "Build succeeded" | tee -a "$LOG_FILE"
else
  echo "::error ::Build failed!" | tee -a "$LOG_FILE"
  exit 1
fi
