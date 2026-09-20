#!/usr/bin/env bash
# Checks the VERSION file against the released tags.
#
#   scripts/check-version.sh            # CI: VERSION must be x.y.z and newer than every v* tag,
#                                       #     unless HEAD is the commit that tag points at
#   scripts/check-version.sh --release  # also: tag v<VERSION> must not exist yet and
#                                       #       CHANGELOG.md must have a "## <VERSION>" section
set -euo pipefail
cd "$(dirname "$0")/.."

fail() { echo "check-version: $*" >&2; exit 1; }

version=$(tr -d '[:space:]' < VERSION)
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "VERSION must be x.y.z, got '$version'"

latest=$(git tag --list 'v*' | sed 's/^v//' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n 1 || true)
if [ -n "$latest" ]; then
  newest=$(printf '%s\n%s\n' "$latest" "$version" | sort -V | tail -n 1)
  if [ "$version" = "$latest" ]; then
    if ! git describe --tags --exact-match HEAD 2>/dev/null | grep -qx "v$version"; then
      fail "VERSION $version is already released (tag v$version); bump it"
    fi
  elif [ "$newest" != "$version" ]; then
    fail "VERSION $version is older than the latest release $latest"
  fi
fi

if [ "${1:-}" = "--release" ]; then
  if git rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
    fail "tag v$version already exists; bump VERSION"
  fi
  grep -qE "^## \[?$version\]?( |$)" CHANGELOG.md || fail "CHANGELOG.md has no '## $version' section"
fi

echo "check-version: $version ok${latest:+ (latest release $latest)}"
