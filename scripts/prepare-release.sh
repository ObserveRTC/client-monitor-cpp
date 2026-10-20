#!/usr/bin/env bash
# Prepares the working tree for release X.Y.Z; the Release workflow runs it, and it runs locally too:
#
#   scripts/prepare-release.sh 0.4.0 [notes-file] [prompt-file]
#
#   - sets VERSION in project(...) in CMakeLists.txt;
#   - turns the `## Unreleased` section of CHANGELOG.md into `## X.Y.Z` (an existing `## X.Y.Z`
#     section is kept as it is);
#   - points the version pins in README.md at vX.Y.Z;
#   - writes the section's text, the release notes, to notes-file (default: release-notes.md);
#   - writes the upgrade prompt to prompt-file (default: upgrade-prompt.md): docs/upgrade-prompt.md
#     filled in with the version, the previous version, the upgrade steps from
#     docs/upgrade/X.Y.Z.md and the release notes. It is attached to the release, for handing to a
#     coding agent that updates an application.
#
# It refuses a version that is not X.Y.Z, is lower than the current one, has no notes, or is
# breaking (its notes say **Breaking**) without docs/upgrade/X.Y.Z.md. A refused release leaves the
# tree as it was. Running it again with the same version changes nothing.
set -euo pipefail

version="${1:-}"
notes="${2:-release-notes.md}"
prompt="${3:-upgrade-prompt.md}"

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

# The upgrade prompt. A breaking release has to say how to upgrade.
steps_file="docs/upgrade/${version}.md"

if [[ ! -f "${steps_file}" ]] && grep -q '\*\*Breaking' "${notes}"; then
	echo "the ${version} notes say **Breaking**, but there is no ${steps_file} with the upgrade steps" >&2
	rm -f "${notes}"
	exit 1
fi

# The version before this one: the first version heading below the released section.
previous="$(awk -v section="${section}" '
	$0 ~ "^## " { if (seen && $2 ~ /^[0-9]+\.[0-9]+\.[0-9]+$/) { print $2; exit } if ($2 == section) seen = 1 }
' CHANGELOG.md)"
previous="${previous:-the previous version}"

steps_tmp="$(mktemp)"
trap 'rm -f "${steps_tmp}"' EXIT

if [[ -f "${steps_file}" ]]; then
	cat "${steps_file}" > "${steps_tmp}"
else
	printf '%s\n' "This release lists no breaking changes and no upgrade steps. Read the release notes below" \
		"and adapt anything this project uses that they change." > "${steps_tmp}"
fi

awk -v version="${version}" -v previous="${previous}" -v steps="${steps_tmp}" -v notes="${notes}" '
	$0 == "{{UPGRADE_STEPS}}" { while ((getline line < steps) > 0) print line; next }
	$0 == "{{RELEASE_NOTES}}" { while ((getline line < notes) > 0) print line; next }
	{ gsub(/\{\{VERSION\}\}/, version); gsub(/\{\{PREVIOUS_VERSION\}\}/, previous); print }
' docs/upgrade-prompt.md > "${prompt}"

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

echo "prepared ${version} (was ${current}); notes in ${notes}, upgrade prompt in ${prompt}"
