#!/proc/boot/ksh
# Same-unit, on-device backup restoration. No source firmware is uploaded.
set -eu
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
umask 077
G=$1
case "$G" in ''|*[!a-f0-9]*) exit 90;; esac
[ "${#G}" -eq 16 ] || exit 90
B=/persistence/.v43b/$G
C=/persistence/.v43c/$G
L=/persistence/.v43l/$G
T=$C/control-helper
[ ! -L "$B/pm.cfg.original" ] || exit 91
[ -f "$B/pm.cfg.original" ] || [ -f "$B/pm.cfg.absent" ] || exit 91
# Restore a first-install redirect from its verified same-unit backup. Never
# overwrite an unknown independently changed rc or run any recovered script.
if [ -f "$B/rc.original" ]; then
    [ ! -L "$B/rc.original" ] && [ -f "$B/rc.original.digest" ] || exit 98
    [ -x "$T" ] && [ ! -L "$T" ] || exit 98
    "$T" --digest "$B/rc.original" > "$C/rc.restore-check"
    read H1 N1 < "$B/rc.original.digest"
    read H2 N2 < "$C/rc.restore-check"
    [ "$H1:$N1" = "$H2:$N2" ] || exit 98
    "$T" --digest /ffs/etc/rc > "$C/rc.current-check"
    read HC NC < "$C/rc.current-check"
    [ "$HC:$NC" = "$H1:$N1" ] ||
        [ "$HC:$NC" = '8591e0a88dbbde53cc9425e1d4a62f8dab52bef1ae0d8e2c9cecd6e307c5d205:3732' ] || exit 99
    mount -uw /ffs
    cp -p "$B/rc.original" /ffs/etc/.rc-v43-restore-$G
    "$T" --digest /ffs/etc/.rc-v43-restore-$G > "$C/rc.restore-check"
    read H2 N2 < "$C/rc.restore-check"
    [ "$H1:$N1" = "$H2:$N2" ] || exit 98
    sync
    mv /ffs/etc/.rc-v43-restore-$G /ffs/etc/rc
    sync
    mount -ur /ffs
fi
# This tells the existing bootstrap not to re-arm the shim.
mkdir -p /persistence/.v43c
: > /persistence/.v43c/DISABLED
if [ -e /extbin/apps/bin/mm-ipod ]; then
    [ ! -L /extbin/apps/bin/mm-ipod ] &&
        grep V43_PUBLIC_SHIM /extbin/apps/bin/mm-ipod >/dev/null 2>&1 || exit 92
    mount -uw /extbin
    rm -f /extbin/apps/bin/mm-ipod
    sync
    mount -ur /extbin
fi
if [ -f "$B/pm.cfg.original" ]; then
    cp -p "$B/pm.cfg.original" /persistence/pm.cfg.v43-restore
    sync
    mv /persistence/pm.cfg.v43-restore /persistence/pm.cfg
else
    rm -f /persistence/pm.cfg
fi
for F in .h .c .n .m; do
    if [ -f "$B/$F.original" ]; then
        cp -p "$B/$F.original" /persistence/$F.restore
        mv /persistence/$F.restore /persistence/$F
    elif [ -f "$B/$F.absent" ]; then
        rm -f /persistence/$F
    fi
done
if [ -f "$B/ACTIVE.original" ]; then
    cp -p "$B/ACTIVE.original" /persistence/.v43c/ACTIVE.restore
    mv /persistence/.v43c/ACTIVE.restore /persistence/.v43c/ACTIVE
    rm -f /persistence/.v43c/DISABLED
else
    rm -f /persistence/.v43c/ACTIVE
fi
sync
echo "V43_ROLLBACK_RESTORED $B; restart manually; current native process was not killed"
