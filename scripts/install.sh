#!/usr/bin/env bash
set -euo pipefail

version="${1:-latest}"
destination="${2:-}"
if (( $# > 2 )); then
    echo "Usage: bash install.sh [latest|VERSION] [DESTINATION]" >&2
    exit 1
fi
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo "This installer supports Linux x64 only." >&2
    exit 1
fi
for tool in curl unzip zipinfo sha256sum; do
    command -v "$tool" >/dev/null || { echo "Required tool missing: $tool" >&2; exit 1; }
done
if [[ "$version" == latest ]]; then
    if ! release="$(curl --fail --silent --show-error --location --connect-timeout 15 --max-time 180 \
        https://api.github.com/repos/jfbilodeau/Nomad/releases/latest)"; then
        echo "Cannot resolve the latest stable release. Specify a version explicitly to install a prerelease." >&2
        exit 1
    fi
    if printf '%s\n' "$release" | grep -Eq '"(draft|prerelease)"[[:space:]]*:[[:space:]]*true'; then
        echo "GitHub did not return a stable published release." >&2
        exit 1
    fi
    version="$(printf '%s\n' "$release" | sed -nE 's/.*"tag_name"[[:space:]]*:[[:space:]]*"(v[0-9]+\.[0-9]+\.[0-9]+)".*/\1/p')"
fi
version="${version#v}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Version must be latest or major.minor.patch (optionally prefixed with v)." >&2
    exit 1
fi
destination="${destination:-$HOME/.nomad/sdks/$version/linux-x64}"
if [[ -e "$destination" || -L "$destination" ]]; then
    echo "Installation already exists: $destination" >&2
    exit 1
fi
mkdir -p "$(dirname "$destination")"
parent="$(cd "$(dirname "$destination")" && pwd)"
destination="$parent/$(basename "$destination")"
staging="$(mktemp -d "$parent/.nomad-install-XXXXXXXX")"
trap 'rm -rf -- "$staging"' EXIT
name="nomad-sdk-linux-x64-$version.zip"
base="https://github.com/jfbilodeau/Nomad/releases/download/v$version"
curl --fail --silent --show-error --location --connect-timeout 15 --max-time 180 \
    "$base/SHA256SUMS.txt" -o "$staging/SHA256SUMS.txt"
curl --fail --silent --show-error --location --connect-timeout 15 --max-time 180 \
    "$base/$name" -o "$staging/$name"
awk -v name="$name" '$2 == name && length($1) == 64 && $1 !~ /[^0-9a-fA-F]/ { print }' \
    "$staging/SHA256SUMS.txt" > "$staging/checksum"
if [[ "$(wc -l < "$staging/checksum")" -ne 1 ]]; then
    echo "Expected exactly one checksum for $name" >&2
    exit 1
fi
(cd "$staging" && sha256sum --check --strict checksum)
unzip -Z -1 "$staging/$name" > "$staging/entries"
if ! awk '/^\// || /\\/ || /:/ || /(^|\/)\.\.(\/|$)/ { exit 1 }' "$staging/entries"; then
    echo "Unsafe archive path." >&2
    exit 1
fi
zipinfo -l "$staging/$name" > "$staging/metadata"
if grep -q '^l' "$staging/metadata"; then
    echo "Archive symbolic links are not supported." >&2
    exit 1
fi
mkdir "$staging/sdk"
unzip -q "$staging/$name" -d "$staging/sdk"
for tool in nomad nomadc nomad-runtime; do
    [[ -x "$staging/sdk/$tool" ]] || { echo "Missing executable SDK tool: $tool" >&2; exit 1; }
done
[[ -d "$staging/sdk/templates" && -f "$staging/sdk/runtime/runtime.json" ]] || {
    echo "Missing SDK templates or runtime manifest." >&2; exit 1;
}
manifest="$staging/sdk/runtime/runtime.json"
grep -Eq "\"version\"[[:space:]]*:[[:space:]]*\"${version//./\\.}\"" "$manifest" &&
    grep -Eq '"target"[[:space:]]*:[[:space:]]*"linux-x64"' "$manifest" || {
        echo "SDK runtime version or target does not match the requested installation." >&2; exit 1;
    }
mv -T -n "$staging/sdk" "$destination"
if [[ -d "$staging/sdk" ]]; then
    echo "Installation destination was created by another process: $destination" >&2
    exit 1
fi
echo "Installed Nomad $version to $destination"
printf 'Add this directory to PATH: export PATH=%q:"$PATH"\n' "$destination"
