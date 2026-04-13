#!/usr/bin/env sh

set -eu

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

"$script_dir/install-git-hooks.sh"

printf '%s\n' "Repository setup complete"
