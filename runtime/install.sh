#!/proc/boot/ksh
set -eu
USB=$1
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
umask 077
[ "$(id -u)" = 0 ] || { echo 'Root Testmode required';exit 1; }
[ -f "$USB/model.conf" ] && [ ! -L "$USB/model.conf" ] || exit 2
read MODEL FPS < "$USB/model.conf"
case "$MODEL:$FPS" in 790:30|791:40) :;;*) exit 2;;esac
# Never start a fresh transaction while another installer/failed one owns it.
LOCK=/tmp/cns2-free-usb-install.lock
(set -C;echo $$ > "$LOCK") 2>/dev/null || { echo 'Installer already owns lock; inspect logs before retrying';exit 3; }
G=;SERIAL=;ENTERED=0;DONE=0;TOOL=
LOG=$USB/cns2-free-install-latest.log
: > "$LOG"
log(){ echo "$*";echo "$*" >> "$LOG"; }
finish(){
 RC=$?
 if [ "$DONE" -ne 1 ];then
  log "INSTALL_STOP rc=$RC; NO automatic reboot; SSH/network not stopped"
  if [ "$ENTERED" -eq 1 ];then /proc/boot/ksh "$USB/runtime/maintenance.sh" release "$G" "$SERIAL" "$TOOL" >> "$LOG" 2>&1 || :;fi
 fi
 if [ -n "$G" ] && [ -n "$SERIAL" ];then
  REC=/persistence/cns275/install-records/$SERIAL
  mkdir -p "$REC" && cp "$LOG" "$REC/$G.log" || :
  cp "$LOG" "$USB/cns2-free-$MODEL-$G.log" || :
 fi
 [ "$(cat "$LOCK" 2>/dev/null)" != "$$" ] || rm -f "$LOCK"
 sync
}
trap finish 0
trap 'exit 4' 1 2 15
log "CNS2 FREE VIDEO ONLY $MODEL / $FPS FPS -- SSH FIRST"
N=0
while ! mount | awk '$1=="/dev/fs1p0" && $2=="on" && $3=="/ramdisk" && $4=="type" && $5=="flash" {f=1} END{exit f?0:1}';do
 [ "$N" -lt 60 ] || exit 5;sleep 1;N=$((N+1))
done
WORK=/ramdisk/.cns2-free-install-$$
[ ! -e "$WORK" ] && [ ! -L "$WORK" ] || exit 5
mkdir "$WORK"
cp "$USB/payload/cns2-tool" "$WORK/cns2-tool";chmod 700 "$WORK/cns2-tool"
TOOL=$WORK/cns2-tool
"$TOOL" --check-list "$USB/package-proof.txt" "$USB" >> "$LOG" 2>&1
SERIAL=$("$TOOL" --identity);SERIAL=${SERIAL#SERIAL=}
G=$("$TOOL" --generation)
log "SERIAL=$SERIAL installation=$G"
pidin ar > "$WORK/processes"
if awk '$2=="mm-ipod" || $2=="/extbin/apps/usr/bin/mm-ipod" {f=1} END{exit f?0:1}' "$WORK/processes";then log 'Disconnect iPhone before installation';exit 6;fi
LAYOUT=FACTORY
PROOF=$USB/profiles/native-factory-proof.txt
if ! "$TOOL" --check-native "$PROOF" >> "$LOG" 2>&1;then
 LAYOUT=V43_READY;PROOF=$USB/profiles/native-ready-proof.txt
 "$TOOL" --check-native "$PROOF" >> "$LOG" 2>&1
 read OLD < /persistence/.v43c/ACTIVE
 case "$OLD" in ''|*[!a-f0-9]*) exit 7;;esac
 [ "${#OLD}" -eq 16 ] && [ ! -L /persistence/.v43c/$OLD ] || exit 7
 [ "$(cat /persistence/.v43c/$OLD/edition)" = video-only ] || { log 'Refusing paid/unknown downgrade';exit 7; }
 [ "$(cat /persistence/.v43c/$OLD/serial)" = "$SERIAL" ] &&
 [ "$(cat /persistence/.v43c/$OLD/COMMITTED)" = "$OLD $SERIAL" ] || exit 7
fi
log '[1/4] Install/reuse key-only SSH and verify it before any video changes'
/proc/boot/ksh "$USB/runtime/install-ssh.sh" "$USB" "$TOOL" "$SERIAL" "$G" >> "$LOG" 2>&1
[ "$(cat /persistence/cns275/SSH_READY-$G)" = "$G $SERIAL" ] || exit 8
"$TOOL" --ssh-probe >> "$LOG" 2>&1
log '[2/4] Stage video-only files; native HMI, music and navigation stay stock'
for P in .v43c .v43m .v43a .v43l .v43b;do
 [ ! -L /persistence/$P ] || exit 9;mkdir -p /persistence/$P
done
if [ "$LAYOUT" = V43_READY ];then
 /proc/boot/ksh "$USB/runtime/maintenance.sh" enter "$G" "$SERIAL" "$TOOL" >> "$LOG" 2>&1
 ENTERED=1
fi
C=/persistence/.v43c/$G;M=/persistence/.v43m/$G;A=/persistence/.v43a/$G;L=/persistence/.v43l/$G
for P in "$C" "$M" "$A" "$L";do [ ! -e "$P" ] && [ ! -L "$P" ] || exit 9;mkdir "$P";chmod 700 "$P";done
cp "$TOOL" "$C/control-helper";cp "$TOOL" "$L/cns2-tool"
chmod 700 "$C/control-helper" "$L/cns2-tool"
for F in prepare.sh entry.sh maintenance.sh commit.sh rollback.sh;do cp "$USB/runtime/$F" "$C/$F";chmod 700 "$C/$F";done
cp "$PROOF" "$C/boot-native-proof.txt"
echo "$LAYOUT" > "$C/boot-layout";echo "$SERIAL" > "$C/serial"
echo plain > "$C/mode";echo video-only > "$C/edition";echo "$MODEL $FPS" > "$C/profile"
cp "$USB/payload/manifest.txt" "$L/manifest.txt"
while read NAME PERM SIZE HASH;do
 case "$NAME" in *..*|*/*|'') exit 10;;esac
 cp "$USB/payload/$NAME" "$M/$NAME";chmod "$PERM" "$M/$NAME"
done < "$L/manifest.txt"
"$TOOL" --check-list "$L/manifest.txt" "$M" >> "$LOG" 2>&1
for PAIR in '.h hmi' '.m most' '.c carplay' '.n nav';do
 set -- $PAIR
 {
  echo '#!/proc/boot/ksh';echo '# V43_SHORT_ENTRY'
  echo 'G=';echo 'read G < /persistence/.v43c/ACTIVE || exit 81'
  echo "case \"\$G\" in ''|*[!a-f0-9]*) exit 81;;esac"
  echo '[ "${#G}" -eq 16 ] || exit 81'
  echo "exec /persistence/.v43c/\$G/entry.sh $2 \"\$@\""
 } > "$C/$1"
 chmod 700 "$C/$1"
done
if [ "$LAYOUT" = FACTORY ];then "$TOOL" --prepare-rc /ffs/etc/rc "$C/rc.boot-prepared";fi
log '[3/4] Same-unit backup and atomic startup commit (no hot process restart)'
/proc/boot/ksh "$C/commit.sh" "$G" >> "$LOG" 2>&1
[ "$(cat /persistence/.v43c/ACTIVE)" = "$G" ] && [ "$(cat "$C/COMMITTED")" = "$G $SERIAL" ] || exit 11
[ "$("$TOOL" --digest /persistence/pm.cfg)" = "$("$TOOL" --digest "$C/pm.cfg.new")" ] || exit 11
"$TOOL" --check-native "$USB/profiles/native-ready-proof.txt" >> "$LOG" 2>&1
[ "$("$TOOL" --identity)" = "SERIAL=$SERIAL" ] || exit 11
"$TOOL" --ssh-probe >> "$LOG" 2>&1
log "INSTALLED: video-only $MODEL $FPS FPS; SSH/SFTP2222; backup /persistence/.v43b/$G"
log 'NOT INSTALLED: music/cover metadata, navigation metadata, HMI message patch, animation, steering controls'
log '[4/4] Commit verified. Sending one final reboot; visual acceptance still required'
DONE=1
finish
trap - 0
# Testmode returns only after records are durable; no retry sends a reboot.
shutdown -b
