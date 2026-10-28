#!/bin/sh
# Bootstrap outside the checkout; preserve the caller's project directory.
# Cross-platform: portable flags, no macOS-specific arch/cpu pins.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$(uname -s)" in
    Darwin) state="${B_HOME:-$HOME/Library/Application Support/b}" ;;
    Linux)  state="${B_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/b}" ;;
    MINGW*|MSYS*|CYGWIN*) state="${B_HOME:-${XDG_CACHE_HOME:-$HOME/AppData/Local/b}}" ;;
    *)      state="${B_HOME:-${XDG_CACHE_HOME:-$HOME/.cache}/b}" ;;
esac
mkdir -p "$state"
export B_HOME="$state"

# Suite mode: b.c dispatches to native-tool adapters in adapters/.
bin="$state/cli"
stale=0
[ -x "$bin" ] || stale=1
for f in "$here/util/b.c" "$here/util/util.c" "$here/util/b.h" "$here/util/inspect.c" "$here/util/inspect.h" "$here/util/box.c" "$here/util/box.h" "$here/util/annotation.h" "$here"/project/*.c "$here"/project/*.h "$here"/adapters/*.c "$here"/adapters/*.h; do
    [ -e "$f" ] || continue
    [ "$f" -nt "$bin" ] && stale=1
done
if [ "$stale" = 1 ]; then
    temp="$bin.$$"
    trap 'rm -f "$temp"' EXIT
    trap 'rm -f "$temp"; exit 130' HUP INT TERM

    # Probe for C23 support (nullptr keyword) once. Older Apple Clang on
    # GitHub macOS runners rejects -std=gnu23; fall back to gnu17 + nullptr=NULL.
    extra_flags=""
    cc_probe() {
        printf 'int main(void){return nullptr!=0?0:1;}\n' > "$temp.probe.c"
        "${CC:-cc}" -std="$1" -fsyntax-only "$temp.probe.c" 2>/dev/null
    }
    if cc_probe "gnu23"; then
        std_flag="gnu23"
    elif cc_probe "gnu2x"; then
        std_flag="gnu2x"
    else
        std_flag="gnu17"
        # C17 doesn't define nullptr; define it as 0 for compatibility.
        extra_flags="-Dnullptr=0"
    fi

    "${CC:-cc}" -std="$std_flag" -O2 -Wall -Wextra -Werror -I"$here" -I"$here/util" $extra_flags -o "$temp" "$here/util/b.c" "$here/util/util.c" "$here/util/inspect.c" "$here/util/box.c" "$here"/project/*.c "$here"/adapters/*.c
    mv "$temp" "$bin"
    trap - EXIT HUP INT TERM
fi
exec "$bin" "$@"
