#!/proc/boot/ksh
# A real RAM mount and a single bounded shared initializer are mandatory.
set -eu
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
umask 077
G=$1
case "$G" in ''|*[!a-f0-9]*) exit 60;;esac
[ "${#G}" -eq 16 ] || exit 60
C=/persistence/.v43c/$G
M=/persistence/.v43m/$G
L=/persistence/.v43l/$G
O=/ramdisk/.cns2-v43
S=/ramdisk/.cns2-v43-state
[ ! -e /persistence/.v43c/DISABLED ] || exit 61
N=0
while ! mount | awk '$1=="/dev/fs1p0" && $2=="on" && $3=="/ramdisk" && $4=="type" && $5=="flash" {f=1} END {exit f?0:1}';do
 [ ! -e /persistence/.v43c/DISABLED ] || exit 61
 [ "$N" -lt 60 ] || exit 62
 sleep 1;N=$((N+1))
done
[ ! -e /ramdisk/.cns2-v43-maint/HOLD ] || exit 61
[ ! -L "$S" ] && [ ! -L "$O" ] || exit 62
mkdir -p "$S" "$O";chmod 700 "$S" "$O"
[ ! -f "$S/FAILED" ] || exit 64
if [ -f "$S/READY" ];then [ "$(cat "$S/READY")" = "$G" ];exit $?;fi
if ! (set -C;echo $$ > "$S/prepare.lock") 2>/dev/null;then
 N=0
 while [ "$N" -lt 60 ];do
  [ ! -f "$S/FAILED" ] || exit 64
  if [ -f "$S/READY" ];then [ "$(cat "$S/READY")" = "$G" ];exit $?;fi
  sleep 1;N=$((N+1))
 done
 exit 65
fi
SUCCESS=0
trap 'RC=$?;if [ "$SUCCESS" -ne 1 ];then echo "FAILED $RC $G" > "$S/FAILED";fi;rm -f "$S/prepare.lock"' 0
trap 'exit 66' 1 2 15
TOOL=$L/cns2-tool
read SERIAL < "$C/serial"
[ "$(cat "$C/edition")" = video-only ] && [ "$(cat "$C/COMMITTED")" = "$G $SERIAL" ] || exit 68
N=0
while :;do
 if RESULT=$("$TOOL" --identity);then break;fi
 N=$((N+1));[ "$N" -lt 3 ] || exit 69;sleep 1
done
[ "$RESULT" = "SERIAL=$SERIAL" ] || exit 70
if [ -f /persistence/.v43c/MAINTENANCE ];then
 read MT MO MS < /persistence/.v43c/MAINTENANCE
 [ "$MT" = "$G" ] && [ "$MS" = "$SERIAL" ] || exit 81
fi
cp "$L/manifest.txt" "$O/manifest.txt"
while read NAME PERM SIZE HASH;do
 case "$NAME" in
  libcns2-most-pull-r43.so|libcns2-autoload-carplay-v9.so|libcns2-carplay-altscreen-live-v9-showui-persistlog.so|CarPlay-cpmap-v2.sh|cpmap-bootstrap.sh) :;;
  *) exit 74;;
 esac
 [ -f "$M/$NAME" ] && [ ! -L "$M/$NAME" ] || exit 74
 cp "$M/$NAME" "$O/$NAME.part";chmod "$PERM" "$O/$NAME.part";mv "$O/$NAME.part" "$O/$NAME"
done < "$O/manifest.txt"
"$TOOL" --check-list "$O/manifest.txt" "$O" >> "$S/order.log" 2>&1
mkdir -p /ramdisk/cns275-carplayvc;chmod 700 /ramdisk/cns275-carplayvc
if [ -f /persistence/.v43c/MAINTENANCE ];then mv /persistence/.v43c/MAINTENANCE "$C/maintenance-reboot-complete";fi
echo "$G" > "$S/READY.part";mv "$S/READY.part" "$S/READY"
SUCCESS=1
echo "VIDEO_ONLY_READY $G" >> "$S/order.log"
