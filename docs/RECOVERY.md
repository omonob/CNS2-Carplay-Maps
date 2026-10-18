# Stop and preserve access

If installation stops, do not repeatedly run it or reboot a vehicle blindly.
Keep the USB log and identify whether SSH, staging or commit failed. A failure
before `SSH_READY` cannot publish a video startup entry. If SSH succeeds but
video fails, SSH stays available; the installer does not kill its listener.

Native programs are not hot-restarted during installation. An interrupted
commit has same-unit backups under `/persistence/.v43b/<generation>`; the
transaction's rollback script only accepts the matching active generation.
Do not copy backups from another unit or manually delete `.v43*` directories.

If a completed video installation needs rollback, have an authorized maintainer
inspect the active generation, same-unit ID, backup receipt and current native
state before invoking its rollback. Phone must be disconnected, vehicle
stationary, and a new reboot authorized. This is not an unconditional USB
factory reset or a firmware recovery image. It does not undo owner SSH keys.

An existing paid/unknown install or unresolved maintenance state intentionally
stops free installation. Use the paid maintainer's verified recovery workflow;
do not force a free downgrade.
