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

# Suite mode: src/b.sh.c dispatches to the verbs; adapters/ are native-tool edges.
bin="$state/cli"
stale=0
[ -x "$bin" ] || stale=1
for f in "$here"/b/*.c "$here"/b/*.h "$here"/adapters/*.c "$here"/adapters/*.h "$here"/adapters/lang/*.c "$here"/adapters/lang/*.h; do
    [ -e "$f" ] || continue
    [ "$f" -nt "$bin" ] && stale=1
done
if [ "$stale" = 1 ]; then
    temp="$bin.$$"
    cleanup() { rm -f "$temp" "$temp.probe.c"; }
    trap cleanup EXIT
    trap 'cleanup; exit 130' HUP INT TERM

    # Probe for C23 support (nullptr keyword) once. Older Apple Clang on
    # GitHub macOS runners rejects -std=gnu23; fall back to gnu17 + nullptr=NULL.
    extra_flags=""
    cc_probe() {
        printf 'int main(void){return nullptr!=0?0:1;}\n' > "$temp.probe.c"
        probe_status=0
        "${CC:-cc}" -std="$1" -fsyntax-only "$temp.probe.c" 2>/dev/null || probe_status=$?
        rm -f "$temp.probe.c"
        return "$probe_status"
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

    "${CC:-cc}" -std="$std_flag" -O2 -Wall -Wextra -Werror -I"$here" -I"$here/.." -I"$here/b" $extra_flags -o "$temp" "$here"/b/*.c "$here"/adapters/*.c "$here"/adapters/lang/*.c
    mv "$temp" "$bin"
    trap - EXIT HUP INT TERM
fi
exec "$bin" "$@"
