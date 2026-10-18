#!/proc/boot/ksh
# Ownership token only. No music/cover/RGI/animation/control helper is launched.
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
LOCK=/ramdisk/cns275-carplayvc/v43-bootstrap.lock
(set -C;echo $$ > "$LOCK") 2>/dev/null || exit 0
trap '[ "$(cat "$LOCK" 2>/dev/null)" != "$$" ] || rm -f "$LOCK"' 0
trap 'exit 0' 1 2 15
while [ ! -e /persistence/.v43c/DISABLED ] && [ ! -e /ramdisk/.cns2-v43-maint/HOLD ];do sleep 2;done
