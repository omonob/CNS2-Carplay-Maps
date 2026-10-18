# Offline build

Requires an existing, lawfully configured ARM/QNX 6.5 toolchain container
(`qnx65-armv7-toolchain:8.5-cpp`) and Python 3. The SDK/toolchain/license is not
distributed and the build never downloads it. Do not change Docker permissions.

Run `tools/build.sh` inside that container with the repository mounted at
`/work` and a local tuning file passed as its single argument. The local file
must provide `RECOVERY_790` and `RECOVERY_791`; it is not a public profile table.
The public profiles fix 790 at 30 FPS and 791 at 40 FPS. Do not commit local
tuning, build command transcripts, customer IDs or validation fixtures.

Then run `python3 tests/test_offline.py`, `python3 tests/test_ssh_gate.py`,
`python3 tools/package.py` and `python3 tests/test_release.py` from
the repository root. The latter creates two separate ready-to-copy USB roots,
two ZIP release assets, and SHA256SUMS under ignored `dist/`. Each package is
self-contained; never combine their payload directories.

`tools/manifest.py <USB-root>` regenerates the package BLAKE2b-256 list after
an authorized public-key customization. It does not accept private keys.
Package hashes detect accidental corruption, not malicious replacement of a
whole unsigned package. Download only from the owner's repository.

Tests run isolated host fixtures. They never SSH to a vehicle, invoke vehicle
shutdown, mount a real vehicle filesystem, or alter the paid/golden packages.
