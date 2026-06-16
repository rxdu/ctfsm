#!/usr/bin/env bash
# Activate the version-controlled git hooks in scripts/hooks/ for this clone.
# One-time per clone (core.hooksPath is local config, not committed).
#
#   scripts/hooks/install.sh           # enable
#   git config --unset core.hooksPath  # disable
#
# Hooks: pre-commit (clang-format staged files) · pre-push (clang-tidy gate).
# Both prefer the pinned tools in .venv (requirements-dev.txt) and degrade to a
# notice if a tool or build is missing — CI remains the real gate either way.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

chmod +x scripts/hooks/pre-commit scripts/hooks/pre-push
git config core.hooksPath scripts/hooks
echo "git hooks activated (core.hooksPath -> scripts/hooks):"
echo "  pre-commit  clang-format on staged C/C++"
echo "  pre-push    clang-tidy gate (scripts/tidy.sh)"
