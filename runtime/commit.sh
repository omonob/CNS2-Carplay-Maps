#!/proc/boot/ksh
# Commit only after every staged upload has been checked locally on the unit.
set -eu
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
umask 077
G=$1
case "$G" in ''|*[!a-f0-9]*) exit 90;; esac
[ "${#G}" -eq 16 ] || exit 90
C=/persistence/.v43c/$G
L=/persistence/.v43l/$G
B=/persistence/.v43b/$G
T=$C/control-helper
[ -x "$T" ] && [ ! -L "$T" ] || exit 98
LAYOUT=
read LAYOUT < "$C/boot-layout"
case "$LAYOUT" in
 FACTORY) PM_SOURCE=/etc/procman_core.cfg;;
 LEGACY_READY|V43_READY) PM_SOURCE=/persistence/pm.cfg;;
 *) echo V43_UNKNOWN_BOOT_LAYOUT; exit 98;;
esac
"$T" --check-native "$C/boot-native-proof.txt"
if [ "$LAYOUT" = FACTORY ]; then
    [ ! -e /persistence/.v43c/MAINTENANCE ] &&
        [ ! -e /ramdisk/.cns2-v43-maint/HOLD ] &&
        [ ! -e /persistence/.v43c/DISABLED ] || { echo V437_FACTORY_MAINTENANCE_CONFLICT_STOP;exit 102; }
else
    # Updates must enter verified project-only maintenance first.
    [ -f /persistence/.v43c/MAINTENANCE ] &&
        [ ! -L /persistence/.v43c/MAINTENANCE ] &&
        [ -f /ramdisk/.cns2-v43-maint/HOLD ] &&
        [ ! -L /ramdisk/.cns2-v43-maint/HOLD ] || exit 102
    read MT MO MS < /persistence/.v43c/MAINTENANCE
    read HT HO HS < /ramdisk/.cns2-v43-maint/HOLD
    read ACTIVE_BEFORE < /persistence/.v43c/ACTIVE
    read TARGET_SERIAL < "$C/serial"
    [ "$MT:$MO:$MS" = "$HT:$HO:$HS" ] &&
        [ "$MT" = "$G" ] && [ "$MO" = "$ACTIVE_BEFORE" ] &&
        [ "$MS" = "$TARGET_SERIAL" ] || exit 102
fi
LOCK=/persistence/.v43c/install.lock
read COMMIT_SERIAL < "$C/serial"
if ! (set -C; echo "$$ $G $COMMIT_SERIAL" > "$LOCK") 2>/dev/null; then
    echo V43_INSTALL_BUSY
    exit 91
fi
DONE=0
BACKUP_READY=0
trap 'RC=$?; if [ "$DONE" -ne 1 ]; then echo "V43_COMMIT_INCOMPLETE rc=$RC backup=$B"; if [ "$BACKUP_READY" -eq 1 ]; then /proc/boot/ksh "$C/rollback.sh" "$G"; fi; fi; rm -f "$LOCK"' 0
# The dispatcher ignores HUP before starting this transaction in the
# background. Preserve that disposition so an SSH drop cannot kill commit.
trap 'exit 92' 2 15
[ ! -e "$B" ] && [ ! -L "$B" ] || exit 93
mkdir "$B" && chmod 700 "$B"
[ ! -L /persistence/pm.cfg ] && [ ! -L "$PM_SOURCE" ] && [ -f "$PM_SOURCE" ] || exit 94
if [ -f /persistence/pm.cfg ]; then
    echo "BACKUP /persistence/pm.cfg -> $B/pm.cfg.original"
    cp -p /persistence/pm.cfg "$B/pm.cfg.original"
else
    : > "$B/pm.cfg.absent"
fi
echo "ACTIVE_SOURCE $PM_SOURCE -> $B/pm.active-before.cfg"
cp -p "$PM_SOURCE" "$B/pm.active-before.cfg"
if [ "$LAYOUT" = FACTORY ]; then
    echo "BACKUP /ffs/etc/rc -> $B/rc.original (same unit, no download)"
    cp -p /ffs/etc/rc "$B/rc.original"
    "$T" --digest /ffs/etc/rc > "$B/rc.original.digest"
    "$T" --digest "$B/rc.original" > "$C/rc.backup.digest"
    read H1 N1 < "$B/rc.original.digest"
    read H2 N2 < "$C/rc.backup.digest"
    [ "$H1" = "$H2" ] && [ "$N1" = "$N2" ] || exit 97
    "$T" --digest "$C/rc.boot-prepared" > "$C/rc.prepared.digest"
    read H2 N2 < "$C/rc.prepared.digest"
    [ "$H2:$N2" = '8591e0a88dbbde53cc9425e1d4a62f8dab52bef1ae0d8e2c9cecd6e307c5d205:3732' ] || exit 99
fi
for F in .h .c .n .m; do
    if [ -e /persistence/$F ]; then
        [ ! -L /persistence/$F ] || exit 95
        grep V43_SHORT_ENTRY /persistence/$F >/dev/null 2>&1 || exit 95
        cp -p /persistence/$F "$B/$F.original"
    else
        : > "$B/$F.absent"
    fi
done
if [ -f /persistence/.v43c/ACTIVE ]; then
    cp -p /persistence/.v43c/ACTIVE "$B/ACTIVE.original"
else
    : > "$B/ACTIVE.absent"
fi
[ ! -e /extbin/apps/bin/mm-ipod ] || {
    [ ! -L /extbin/apps/bin/mm-ipod ] &&
        grep V43_PUBLIC_SHIM /extbin/apps/bin/mm-ipod >/dev/null 2>&1 || exit 96
    cp -p /extbin/apps/bin/mm-ipod "$B/mm-ipod-shim.original"
}
sync
"$T" --digest "$PM_SOURCE" > "$C/pm.before"
"$T" --digest "$B/pm.active-before.cfg" > "$C/pm.backup"
read H1 N1 < "$C/pm.before"
read H2 N2 < "$C/pm.backup"
[ "$H1" = "$H2" ] && [ "$N1" = "$N2" ] || exit 97
BACKUP_READY=1
# Patch locally, never download an original firmware file to the PC.
"$T" --patch-pm "$B/pm.active-before.cfg" "$C/pm.cfg.new"
# All wrappers are identical to those from the locally validated staging set.
for F in .h .c .n .m; do
    cp "$C/$F" /persistence/$F.new && chmod 700 /persistence/$F.new
    mv /persistence/$F.new /persistence/$F
done
echo "$G" > /persistence/.v43c/ACTIVE.new
chmod 600 /persistence/.v43c/ACTIVE.new
mv /persistence/.v43c/ACTIVE.new /persistence/.v43c/ACTIVE
cp "$C/pm.cfg.new" /persistence/pm.cfg.v43-new
chmod 600 /persistence/pm.cfg.v43-new
sync
mv /persistence/pm.cfg.v43-new /persistence/pm.cfg
rm -f /persistence/.v43c/DISABLED
sync
# Publish the cold-boot redirect LAST. A failure before this leaves stock rc
# selecting its original config. Do not restart procman or any native process.
if [ "$LAYOUT" = FACTORY ]; then
    mount -uw /ffs
    RC_NEW=/ffs/etc/.rc-v43-new-$G
    [ ! -e "$RC_NEW" ] && [ ! -L "$RC_NEW" ] || exit 100
    cp -p "$B/rc.original" "$RC_NEW"
    cat "$C/rc.boot-prepared" > "$RC_NEW"
    "$T" --digest "$RC_NEW" > "$C/rc.staged.digest"
    read H1 N1 < "$C/rc.staged.digest"
    read H2 N2 < "$C/rc.prepared.digest"
    [ "$H1:$N1" = "$H2:$N2" ] || exit 101
    sync
    echo 'COLD_BOOT_ENTRY /ffs/etc/rc -> /persistence/pm.cfg; stock runHMI not changed'
    mv "$RC_NEW" /ffs/etc/rc
    sync
    mount -ur /ffs
fi
DONE=1
# Explicit success cleanup as well as the exit trap, to avoid leaving a
# legacy generation-only lock when SSH closes around script completion.
[ "$(cat "$LOCK")" = "$$ $G $COMMIT_SERIAL" ] || exit 103
rm -f "$LOCK"
sync
[ ! -e "$LOCK" ] || exit 103
echo "$G $COMMIT_SERIAL" > "$C/COMMITTED.new"
chmod 600 "$C/COMMITTED.new"
mv "$C/COMMITTED.new" "$C/COMMITTED"
sync
echo "V43_COMMITTED generation=$G backup=$B NO_REBOOT NO_HOT_RESTART"
