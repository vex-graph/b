#!/bin/sh
# Bootstrap outside the checkout; preserve the caller's project directory.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$(uname -s)" in
    Darwin) state="${B_HOME:-$HOME/Library/Application Support/b}" ;;
    *) state="${B_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/b}" ;;
esac
mkdir -p "$state"

# Suite mode: b.c dispatches to native-tool adapters in adapters/.
bin="$state/cli"
stale=0
[ -x "$bin" ] || stale=1
for f in "$here/b.c" "$here/util.c" "$here/util.h" "$here/b.h" "$here/inspect.c" "$here/inspect.h" "$here/annotation.h" "$here"/adapters/*.c "$here"/adapters/*.h; do
    [ -e "$f" ] || continue
    [ "$f" -nt "$bin" ] && stale=1
done
if [ "$stale" = 1 ]; then
    temp="$bin.$$"
    trap 'rm -f "$temp"' EXIT
    trap 'rm -f "$temp"; exit 130' HUP INT TERM
    case "$(uname -s)" in
        Darwin) "${CC:-cc}" -std=gnu23 -O2 -Wall -Wextra -Werror -I"$here" -arch arm64 -mcpu=apple-m1 -mmacosx-version-min=14.0 "$here/b.c" "$here/util.c" "$here/inspect.c" "$here"/adapters/*.c -o "$temp" ;;
        *) "${CC:-cc}" -std=gnu23 -O2 -Wall -Wextra -Werror -I"$here" "$here/b.c" "$here/util.c" "$here/inspect.c" "$here"/adapters/*.c -o "$temp" ;;
    esac
    mv "$temp" "$bin"
    trap - EXIT HUP INT TERM
fi
exec "$bin" "$@"
