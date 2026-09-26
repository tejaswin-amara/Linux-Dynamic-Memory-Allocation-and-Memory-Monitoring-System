#!/usr/bin/env bash
set -euo pipefail

echo "==> Building binaries..."
make all

echo "==> Starting self-hosted mem_monitor under libmyalloc.so..."
export LD_PRELOAD=./libmyalloc.so
exec ./mem_monitor "$@"
