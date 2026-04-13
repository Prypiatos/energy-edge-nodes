#!/usr/bin/env sh

set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

git -C "$repo_root" config core.hooksPath .githooks

printf '%s\n' "Configured Git hooks path to .githooks"
