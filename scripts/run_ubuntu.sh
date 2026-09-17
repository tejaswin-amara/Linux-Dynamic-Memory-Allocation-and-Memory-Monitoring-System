#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PORT=8080
MODE="tui"
RUN_TESTS=0
SETUP_ONLY=0

usage() {
  cat <<'EOF'
Ubuntu runner for Linux Dynamic Memory Allocation & System Task Manager

Usage:
  ./scripts/run_ubuntu.sh                 Build and launch ncurses TUI + Web GUI
  ./scripts/run_ubuntu.sh --headless      Build and run headless monitor + Web GUI
  ./scripts/run_ubuntu.sh --json          Print one JSON snapshot and exit
  ./scripts/run_ubuntu.sh --test          Build and execute the full test suite
  ./scripts/run_ubuntu.sh --setup-only    Install dependencies and build only
  ./scripts/run_ubuntu.sh --port 9090     Use a different HTTP port
  ./scripts/run_ubuntu.sh --help          Show this help

After startup, the Web GUI is available at:
  http://127.0.0.1:<PORT>/
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --headless) MODE="headless"; shift ;;
    --json) MODE="json"; shift ;;
    --test) RUN_TESTS=1; shift ;;
    --setup-only) SETUP_ONLY=1; shift ;;
    --port)
      [[ $# -ge 2 ]] || { echo "ERROR: --port requires a number." >&2; exit 2; }
      PORT="$2"
      shift 2
      ;;
    --help|-h) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ "$(id -u)" -eq 0 ]]; then
  APT="apt-get"
elif command -v sudo >/dev/null 2>&1; then
  APT="sudo apt-get"
else
  echo "ERROR: sudo is required to install Ubuntu build dependencies." >&2
  exit 1
fi

echo "==> Installing Ubuntu dependencies"
$APT update
$APT install -y build-essential gcc make valgrind libncurses-dev clang-format curl

if ! command -v gcc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
  echo "ERROR: gcc/make installation failed." >&2
  exit 1
fi

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || (( PORT < 1 || PORT > 65535 )); then
  echo "ERROR: invalid port: $PORT" >&2
  exit 2
fi

echo "==> Cleaning old build artifacts"
make clean

echo "==> Building project"
make all

if [[ "$RUN_TESTS" -eq 1 ]]; then
  echo "==> Running tests"
  make test
fi

if [[ "$SETUP_ONLY" -eq 1 ]]; then
  echo "==> Setup and build complete"
  exit 0
fi

case "$MODE" in
  tui)
    echo "==> Starting ncurses TUI + Web GUI on port $PORT"
    echo "    Web GUI: http://127.0.0.1:$PORT/"
    echo "    Press q in the TUI or Ctrl+C to stop."
    exec ./mem_monitor --port "$PORT"
    ;;
  headless)
    echo "==> Starting headless monitor + Web GUI on port $PORT"
    echo "    Web GUI: http://127.0.0.1:$PORT/"
    echo "    Press Ctrl+C to stop."
    exec ./mem_monitor --headless --port "$PORT"
    ;;
  json)
    echo "==> Printing one JSON system snapshot"
    exec ./mem_monitor --json
    ;;
esac
