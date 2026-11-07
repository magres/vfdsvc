#!/usr/bin/env bash
# vfdsvc uninstaller — Linux/macOS side. Wraps install.sh --uninstall.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec bash "$HERE/install.sh" "${@/--uninstall/}" --uninstall
