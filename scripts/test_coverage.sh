#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2024
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

LOG_FILE="dist/coverage/test_coverage_log.txt"
COVERAGE_ROOT="dist/coverage"
PROJECT_ROOT=${GITHUB_WORKSPACE:-$(pwd)}
cd "$PROJECT_ROOT"
mkdir -p "$COVERAGE_ROOT"
rm -f "$LOG_FILE"
touch "$LOG_FILE"

echo "🧪 Starting test coverage collection..." | tee -a "$LOG_FILE"

if ! command -v cargo-tarpaulin &>/dev/null; then
  echo "📦 Installing cargo-tarpaulin..." | tee -a "$LOG_FILE"
  cargo install cargo-tarpaulin
fi

export RUSTC_BOOTSTRAP=1

echo "📂 Running tarpaulin for workspace" | tee -a "$LOG_FILE"
mkdir -p "$COVERAGE_ROOT/workspace"

if cargo tarpaulin --workspace --out Html --out Lcov --out Xml \
  --output-dir "$PROJECT_ROOT/$COVERAGE_ROOT/workspace" \
  --ignore-panics --no-fail-fast \
  2>&1 | tee -a "$LOG_FILE"; then
  echo "✅ Coverage generated successfully" | tee -a "$LOG_FILE"
else
  echo "::warning ::tarpaulin failed or no tests found" | tee -a "$LOG_FILE"
fi

echo "✅ All test coverage reports generated at: $COVERAGE_ROOT" | tee -a "$LOG_FILE"
