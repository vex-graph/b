#!/bin/sh
# Bootstrap outside the checkout; preserve the caller's project directory.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$(uname -s)" in
    Darwin) state="${B_HOME:-$HOME/Library/Application Support/b}" ;;
    *) state="${B_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/b}" ;;
esac
source="$here/b.c"
bin="$state/cli"
if [ "${1:-}" = workspace ]; then
    if [ "$#" -lt 3 ]; then
        echo 'b: usage: b workspace <workspace-directory> <command> [arguments...]' >&2
        exit 2
    fi
    shift
    workspace=$1
    shift
    cd "$workspace"
    source="$here/workspace.c"
    bin="$state/workspace-cli"
fi
mkdir -p "$state"
if [ ! -x "$bin" ] || [ "$source" -nt "$bin" ]; then
    temp="$bin.$$"
    trap 'rm -f "$temp"' EXIT
    trap 'rm -f "$temp"; exit 130' HUP INT TERM
    case "$(uname -s)" in
        Darwin) "${CC:-cc}" -std=gnu23 -O2 -Wall -Wextra -Werror -arch arm64 -mcpu=apple-m1 -mmacosx-version-min=14.0 "$source" -o "$temp" ;;
        *) "${CC:-cc}" -std=gnu23 -O2 -Wall -Wextra -Werror "$source" -o "$temp" ;;
    esac
    mv "$temp" "$bin"
    trap - EXIT HUP INT TERM
fi
exec "$bin" "$@"
