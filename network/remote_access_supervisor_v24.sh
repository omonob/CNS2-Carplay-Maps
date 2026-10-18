#!/proc/boot/ksh
# CNS275_REMOTE_ACCESS_V24 CNS2_FREE_SSH_ONLY -- no integrity polling guard.
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
ROOT=/persistence/cns275
LOCK=/tmp/cns275_remote_access_v24.lock
if [ -f "$LOCK" ];then
 read OLD < "$LOCK"
 case "$OLD" in ''|*[!0-9]*) exit 1;;esac
 if pidin ar | awk -v p="$OLD" '$1==p && ($2=="/persistence/cns275/remote_access_supervisor_v24.sh" || ($2=="/proc/boot/ksh" && $3=="/persistence/cns275/remote_access_supervisor_v24.sh")) {f=1} END{exit f?0:1}';then exit 0;fi
 [ ! -L "$LOCK" ] || exit 1
 rm -f "$LOCK"
fi
(set -C;echo $$ > "$LOCK") 2>/dev/null || exit 1
trap '[ "$(cat "$LOCK" 2>/dev/null)" != "$$" ] || rm -f "$LOCK"' 0
trap 'exit 0' 1 2 15
N=0
while :;do
 if ifconfig en0 >/dev/null 2>&1;then
  if ! ifconfig en0 | grep 'inet 172.16.250.248 ' >/dev/null;then ifconfig en0 172.16.250.248 netmask 255.255.255.0 up;fi
 elif [ "$N" -eq 0 ];then
  if [ -x /ffs/sbin/netstart_vag ];then /ffs/sbin/netstart_vag;fi
 fi
 "$ROOT/sshd/start_sshd.sh" || :
 N=$(((N+1)%12))
 sleep 5
done
