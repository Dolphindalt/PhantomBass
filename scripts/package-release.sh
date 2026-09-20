#!/usr/bin/env bash
# Builds the release artifacts with nix and zips them into dist/:
#   PhantomBass-<version>-linux-x86_64.zip
#   PhantomBass-<version>-linux-aarch64.zip
#   PhantomBass-<version>-windows-x86_64.zip
# and writes the CHANGELOG.md section for the version to dist/notes.md.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(tr -d '[:space:]' < VERSION)
rm -rf dist
mkdir -p dist

package() {
  local target=$1 attr=$2
  local name="PhantomBass-$version-$target"
  echo "== $name ($attr)"
  nix build --print-build-logs --out-link "result-$target" ".#$attr"
  rm -rf "dist/$name"
  mkdir -p "dist/$name"
  cp -r --no-preserve=mode "result-$target/lib/vst3/PhantomBass.vst3" "dist/$name/"
  cp --no-preserve=mode "result-$target/lib/clap/PhantomBass.clap" "dist/$name/"
  cp LICENSE THIRD-PARTY.md "dist/$name/"
  (cd dist && zip -qr "$name.zip" "$name" && rm -rf "$name")
  rm -f "result-$target"
}

package linux-x86_64 pbe-plugin
package linux-aarch64 pbe-plugin-aarch64-linux
package windows-x86_64 pbe-plugin-x86_64-windows

# Release notes: the changelog section for this version, up to the next heading.
awk -v v="$version" '
  /^## / { if (found) exit; found = ($2 == v || $2 == "[" v "]") ; next }
  found { print }
' CHANGELOG.md | sed -e '1{/^$/d}' > dist/notes.md
[ -s dist/notes.md ] || { echo "package-release: CHANGELOG.md has no section for $version" >&2; exit 1; }

echo "== dist/"
ls -l dist
