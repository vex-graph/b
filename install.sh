#!/bin/sh
set -eu

fail() {
    printf 'b installer: %s\n' "$1" >&2
    exit 1
}

[ -n "${HOME:-}" ] || fail 'HOME is unset'
version=${B_VERSION:-latest}
case "$version" in
    latest) default_base=https://vex-graph.github.io/b/downloads/latest ;;
    *[!A-Za-z0-9._+-]*|'') fail 'B_VERSION contains unsupported characters' ;;
    *)
        version_tag=$version
        case "$version_tag" in v*) ;; *) version_tag="v$version_tag" ;; esac
        default_base=https://github.com/vex-graph/b/releases/download/$version_tag
        ;;
esac
base=${B_DOWNLOAD_BASE_URL:-$default_base}

system=$(uname -s)
machine=$(uname -m)
case "$system:$machine" in
    Darwin:arm64|Darwin:aarch64) target=darwin-arm64 ;;
    Darwin:x86_64|Darwin:amd64) target=darwin-x86_64 ;;
    Linux:x86_64|Linux:amd64) target=linux-x86_64 ;;
    Linux:aarch64|Linux:arm64) target=linux-arm64 ;;
    *) fail "unsupported platform: $system $machine" ;;
esac

asset="b-$target.tar.gz"
temporary=$(mktemp -d "${TMPDIR:-/tmp}/b-install.XXXXXX") || fail 'cannot create a temporary directory'
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
curl -fsSL "$base/$asset" -o "$temporary/$asset" || fail "cannot download $asset"
curl -fsSL "$base/$asset.sha256" -o "$temporary/$asset.sha256" || fail "cannot download $asset checksum"

expected=$(awk 'NR == 1 { print $1 }' "$temporary/$asset.sha256")
case "$expected" in
    ''|*[!0-9a-fA-F]*) fail 'checksum file is malformed' ;;
esac
[ "${#expected}" -eq 64 ] || fail 'checksum file is malformed'
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$temporary/$asset" | awk '{ print $1 }')
elif command -v shasum >/dev/null 2>&1; then
    actual=$(shasum -a 256 "$temporary/$asset" | awk '{ print $1 }')
else
    fail 'sha256sum or shasum is required to verify the archive'
fi
[ "$(printf '%s' "$expected" | tr 'A-F' 'a-f')" = "$(printf '%s' "$actual" | tr 'A-F' 'a-f')" ] || fail 'archive checksum does not match'

mkdir "$temporary/unpacked"
tar -xzf "$temporary/$asset" -C "$temporary/unpacked" || fail 'cannot unpack the archive'
[ -f "$temporary/unpacked/b" ] && [ -f "$temporary/unpacked/b-core" ] || fail 'archive is incomplete'

install_dir=${B_INSTALL_DIR:-$HOME/.local/bin}
mkdir -p "$install_dir" || fail "cannot create install directory: $install_dir"
install -m 755 "$temporary/unpacked/b" "$install_dir/b"
install -m 755 "$temporary/unpacked/b-core" "$install_dir/b-core"

case "$system" in
    Darwin) default_state="$HOME/Library/Application Support" ;;
    *) default_state=${XDG_STATE_HOME:-$HOME/.local/state} ;;
esac
state_dir=${B_INSTALL_STATE_DIR:-$default_state/b}
mkdir -p "$state_dir" || fail "cannot create install record directory: $state_dir"
record="$state_dir/org.vexgraph.b.record"
record_temp="$record.$$"
printf 'application=org.vexgraph.b\nstate=installed\nversion=%s\ntarget=%s\ninstall_dir=%s\n' \
    "$version" "$target" "$install_dir" > "$record_temp"
mv -f "$record_temp" "$record"

printf 'Installed b for %s in %s\n' "$target" "$install_dir"
case ":${PATH:-}:" in
    *":$install_dir:"*) ;;
    *) printf 'Add this to your shell startup file to use b: export PATH="%s:$PATH"\n' "$install_dir" ;;
esac
