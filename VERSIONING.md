# Versioning policy

XMRigCC uses semantic versions in the form `MAJOR.MINOR.PATCH`.

* Increment `PATCH` (`3.5.1` to `3.5.2`) for backward-compatible bug fixes,
  security fixes, dependency refreshes, build corrections, and internal
  performance or reliability improvements that do not add public behavior.
* Increment `MINOR` and reset `PATCH` (`3.5.2` to `3.6.0`) for backward-compatible
  features, new optional configuration or CLI controls, new algorithms, or
  substantial scalability features.
* Increment `MAJOR` and reset `MINOR` and `PATCH` (`3.6.0` to `4.0.0`) for an
  incompatible CC/Stratum protocol, configuration, CLI, API, or data-format
  change, or for removal of supported public behavior.

The highest-impact change in a release determines its increment. A dependency
upgrade is not automatically a major or minor release; use the effect it has on
users and compatibility.

## Release procedure

1. Update `APP_VERSION` and `APP_VER_MAJOR`, `APP_VER_MINOR`, and
   `APP_VER_PATCH` together in `src/version.h`.
2. Add that version as the first heading in `CHANGELOG.md`.
3. Keep the miner and proxy on the same version when shared protocol or core
   behavior changes.
4. Configure, build, and run the regression tests before tagging.
5. Tag releases as `vMAJOR.MINOR.PATCH`; use prerelease suffixes only before the
   final release, for example `v3.6.0-rc.1`.

`src/version.h` is the canonical version source. CMake reads it into
`PROJECT_VERSION` and fails configuration when its string/numeric fields or the
top changelog heading disagree.
