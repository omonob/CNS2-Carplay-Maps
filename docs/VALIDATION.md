# Candidate validation — 2026-10-19

Status: **offline release candidate; not a new vehicle acceptance claim**.
No vehicle was connected, deployed to or rebooted for this release work.

- ARM/QNX compile: both independent cluster profiles and shared transport,
  autoload and installation helper built successfully with the pre-existing
  cross-toolchain container in offline network mode.
- 9 host helper / workflow tests passed, including an isolated full commit
  and a locally supplied original-rc fixture. The original firmware fixture
  is deliberately not distributed. The checked delta preserves the original
  rc CRC32 and yields the expected startup digest.
- 7 isolated SSH gate tests passed: successful setup, failed host-key
  generation, invalid config, password-enabled config, duplicate directive,
  failed daemon startup and missing SSH banner. Failure cases do not issue an
  SSH_READY receipt. No real SSH daemon or client was contacted by the tests.
- A simulated SSH failure in the real main installer stops before any video
  generation or startup entry is written and does not reboot.
- The isolated full installer simulation keeps same-unit backups and logs,
  publishes the expected four startup entries and sends exactly one mocked
  final reboot. Its SSH service gate is explicitly mocked, not a real login.
- 2 release tests verify the independent model labels, ZIP root layout,
  every package file against its digest list, matching extracted bytes,
  archive SHA-256 checksums, ARM shared libraries, different profile binaries,
  original license notices, and absence of bundled private-key/paid-feature
  payloads. Archive checksums are provided alongside the two downloads.

Still required on stationary supported hardware: cold-start timing, a real
private-key SSH login after exchanging USB devices, each cluster's visible
video, disconnect handback, native-map zoom and a later paid upgrade. These
have not been inferred from offline success. Existing accepted paid-version
results are not counted as acceptance of these new free packages.
