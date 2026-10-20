# Task: update client-monitor-cpp to {{VERSION}}

This project uses client-monitor-cpp (https://github.com/ObserveRTC/client-monitor-cpp), the C++
port of the ObserveRTC client monitor. Version {{VERSION}} is released. Update this project to it
from {{PREVIOUS_VERSION}}, or from whichever version it pins now. The upgrade steps and the release
notes of {{VERSION}} are below. If the project is more than one version behind, also apply the
sections of the versions in between from
https://github.com/ObserveRTC/client-monitor-cpp/blob/v{{VERSION}}/CHANGELOG.md

## 1. Bump the dependency

Find every place the library is pinned (FetchContent `GIT_TAG` or release `URL`, a git submodule,
a vendored copy, a package manifest, CI scripts) and move it to `v{{VERSION}}`. The release archive
is:

    https://github.com/ObserveRTC/client-monitor-cpp/releases/download/v{{VERSION}}/client-monitor-cpp.tar.gz

If a `URL_HASH` is used, take the SHA256 from `client-monitor-cpp.tar.gz.sha256` on the
v{{VERSION}} release. Do this for every platform and build configuration the project has.

## 2. Apply the changes of this release

{{UPGRADE_STEPS}}

## 3. Verify

1. Build every platform and configuration, with warnings as errors if the project normally uses
   them.
2. Run the project's tests.
3. Run the application through a call (or whatever drives the monitor) and check that samples are
   still produced and delivered, with the attachments and events the project relies on.
4. Check the behaviour changes listed in the release notes against what the project does with the
   samples.
5. Summarise what changed in the project, and what could not be verified.

## Release notes of {{VERSION}}
{{RELEASE_NOTES}}
