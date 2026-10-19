#!/usr/bin/env bash
# Prepares the working tree for release X.Y.Z; the Release workflow runs it, and it runs locally too:
#
#   scripts/prepare-release.sh 0.4.0 [notes-file]
#
#   - sets VERSION in project(...) in CMakeLists.txt;
#   - turns the `## Unreleased` section of CHANGELOG.md into `## X.Y.Z` (an existing `## X.Y.Z`
#     section is kept as it is);
#   - points the version pins in README.md at vX.Y.Z;
#   - writes the section's text, the release notes, to notes-file (default: release-notes.md).
#
# It refuses a version that is not X.Y.Z, is lower than the current one, or has no notes. Running it
# again with the same version changes nothing.
set -euo pipefail

version="${1:-}"
notes="${2:-release-notes.md}"

if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "version must be X.Y.Z (no leading v), got '${version}'" >&2
	exit 1
fi

cd "$(dirname "$0")/.."

current="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n 1)"

if [[ -z "${current}" ]]; then
	echo "no VERSION in project(...) in CMakeLists.txt" >&2
	exit 1
fi
# Numeric X.Y.Z comparison that works with the BSD tools on macOS too.
if awk -v a="${version}" -v b="${current}" 'BEGIN {
	split(a, x, "."); split(b, y, ".")
	for (i = 1; i <= 3; i++) { if (x[i] + 0 != y[i] + 0) exit (x[i] + 0 < y[i] + 0) ? 0 : 1 }
	exit 1
}'; then
	echo "version ${version} is lower than the current ${current}" >&2
	exit 1
fi

# The notes come from the version's own section, or from `## Unreleased`. Checked before anything
# is changed, so a refused release leaves the tree as it was.
if grep -q "^## ${version//./\\.}[[:space:]]*$" CHANGELOG.md; then
	section="${version}"
elif grep -q "^## Unreleased[[:space:]]*$" CHANGELOG.md; then
	section="Unreleased"
else
	echo "CHANGELOG.md has neither a '## ${version}' nor a '## Unreleased' section" >&2
	exit 1
fi

awk -v section="${section}" '
	$0 ~ "^## " { if (found) exit; if ($2 == section) { found = 1; next } }
	found { print }
' CHANGELOG.md > "${notes}"

if ! grep -q '[^[:space:]]' "${notes}"; then
	echo "the '## ${section}' section of CHANGELOG.md is empty" >&2
	rm -f "${notes}"
	exit 1
fi

# 1. CMakeLists.txt
sed -i.bak "s/^\([[:space:]]*VERSION[[:space:]]\{1,\}\)${current//./\\.}/\1${version}/" CMakeLists.txt
rm -f CMakeLists.txt.bak

# 2. CHANGELOG.md: `## Unreleased` becomes the version's section.
if [[ "${section}" == "Unreleased" ]]; then
	awk -v version="${version}" '
		!done && /^## Unreleased[[:space:]]*$/ { print "## " version; done = 1; next }
		{ print }
	' CHANGELOG.md > CHANGELOG.md.tmp
	mv CHANGELOG.md.tmp CHANGELOG.md
fi

# 3. README.md version pins.
sed -i.bak -E \
	-e "s#(GIT_TAG[[:space:]]+)v[0-9]+\.[0-9]+\.[0-9]+#\1v${version}#g" \
	-e "s#(releases/download/)v[0-9]+\.[0-9]+\.[0-9]+/#\1v${version}/#g" \
	README.md
rm -f README.md.bak

echo "prepared ${version} (was ${current}); notes in ${notes}"
