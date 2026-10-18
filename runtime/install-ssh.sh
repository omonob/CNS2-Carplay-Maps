#!/proc/boot/ksh
# SSH_FIRST: no video path may be published until this script returns success.
set -eu
USB=$1;TOOL=$2;SERIAL=$3;G=$4
ROOT=/persistence/cns275
SSH=$ROOT/sshd
SRC=$USB/network
export PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
unset SOCK || :
umask 077
[ "$("$TOOL" --identity)" = "SERIAL=$SERIAL" ] || exit 120
[ ! -L "$ROOT" ] && [ ! -L "$SSH" ] || exit 120
mkdir -p "$ROOT/backups"
KEY=$(awk 'NF && $1!~/^#/ {print $1 " " $2;exit}' "$SRC/cns275_sshd/etc/authorized_keys")
case "$KEY" in ssh-rsa\ *) :;;*) exit 121;;esac
grep 'PRIVATE KEY' "$SRC/cns275_sshd/etc/authorized_keys" >/dev/null 2>&1 && exit 121
FRESH=0
if [ ! -e "$SSH" ];then
 FRESH=1
 STAGE=$ROOT/.sshd-free-$G
 [ ! -e "$STAGE" ] && [ ! -L "$STAGE" ] || exit 122
 mkdir "$STAGE"
 cp -R "$SRC/cns275_sshd/." "$STAGE/"
 chmod 700 "$STAGE/usr/sbin/sshd" "$STAGE/usr/bin/ssh-keygen" "$STAGE/usr/libexec/sftp-server" "$STAGE/start_sshd.sh"
 chmod 600 "$STAGE/etc/authorized_keys" "$STAGE/etc/sshd_config"
 export LD_LIBRARY_PATH=$STAGE/usr/lib:/extbin/apps/lib:/ifs2:/proc/boot
 "$STAGE/usr/bin/ssh-keygen" -q -t rsa -b 2048 -N '' -f "$STAGE/etc/ssh_host_rsa_key"
 chmod 600 "$STAGE/etc/ssh_host_rsa_key"
 mkdir "$STAGE/run"
 echo CNS2_FREE_SSH_ONLY > "$STAGE/.free-managed"
 mv "$STAGE" "$SSH"
else
 [ -f "$SSH/etc/sshd_config" ] && [ ! -L "$SSH/etc/authorized_keys" ] && [ -f "$SSH/etc/authorized_keys" ] || exit 123
 # Existing access is never revoked; unknown authentication layouts stop.
 grep '^AuthorizedKeysFile /persistence/cns275/sshd/etc/authorized_keys$' "$SSH/etc/sshd_config" >/dev/null || exit 123
 cp -p "$SSH/etc/authorized_keys" "$ROOT/backups/authorized_keys.before-free-$G"
 if ! awk -v key="$KEY" '$1 " " $2==key{f=1} END{exit f?0:1}' "$SSH/etc/authorized_keys";then
  cp "$SSH/etc/authorized_keys" "$SSH/etc/authorized_keys.free-new"
  echo '' >> "$SSH/etc/authorized_keys.free-new";echo "$KEY" >> "$SSH/etc/authorized_keys.free-new"
  chmod 600 "$SSH/etc/authorized_keys.free-new";mv "$SSH/etc/authorized_keys.free-new" "$SSH/etc/authorized_keys"
 fi
fi
export LD_LIBRARY_PATH=$SSH/usr/lib:/extbin/apps/lib:/ifs2:/proc/boot
[ -f "$SSH/etc/ssh_host_rsa_key" ] && [ ! -L "$SSH/etc/ssh_host_rsa_key" ] || exit 124
"$SSH/usr/bin/ssh-keygen" -l -f "$SRC/cns275_sshd/etc/authorized_keys"
"$SSH/usr/sbin/sshd" -t -f "$SSH/etc/sshd_config"
# -T is unavailable in this old QNX port. Require explicit singleton directives,
# no conditional Match/Include/command overrides, and key-only authentication.
awk '
 /^[ \t]*#/ || NF==0 {next}
 {k=tolower($1);v=tolower($2);count[k]++;value[k]=v;
  if(k=="match"||k=="include"||k=="authorizedkeyscommand"||k=="authenticationmethods")bad=1}
 END{
  if(count["port"]!=1||value["port"]!="2222"||count["pubkeyauthentication"]!=1||value["pubkeyauthentication"]!="yes"||
     count["passwordauthentication"]!=1||value["passwordauthentication"]!="no"||
     count["permitemptypasswords"]!=1||value["permitemptypasswords"]!="no"||
     count["challengeresponseauthentication"]!=1||value["challengeresponseauthentication"]!="no"||
     count["permitrootlogin"]!=1||(value["permitrootlogin"]!="yes"&&value["permitrootlogin"]!="without-password")||
     count["authorizedkeysfile"]!=1||value["authorizedkeysfile"]!="/persistence/cns275/sshd/etc/authorized_keys"||
     count["hostkey"]!=1||value["hostkey"]!="/persistence/cns275/sshd/etc/ssh_host_rsa_key")bad=1;
  exit bad?1:0}' "$SSH/etc/sshd_config" || exit 125
if [ "$FRESH" -eq 1 ];then "$SSH/start_sshd.sh";fi
if ! "$TOOL" --ssh-probe;then
 # Restart only a missing known listener; never stop any listener or stack.
 if [ "$FRESH" -eq 0 ];then
  pidin ar > /tmp/cns2-free-ssh-processes-$G
  if awk '$2 ~ /(^|\/)sshd$/ {f=1} END{exit f?0:1}' /tmp/cns2-free-ssh-processes-$G;then exit 126;fi
  "$SSH/usr/sbin/sshd" -f "$SSH/etc/sshd_config"
 fi
 sleep 1;"$TOOL" --ssh-probe || exit 126
fi
pidin ar > /tmp/cns2-free-ssh-processes-$G
awk -v bin="$SSH/usr/sbin/sshd" -v cfg="$SSH/etc/sshd_config" '$2==bin && $3=="-f" && $4==cfg {f=1} END{exit f?0:1}' /tmp/cns2-free-ssh-processes-$G || exit 127
if [ -f "$SSH/.free-managed" ];then
 # First install only. Existing network startup and existing Telnet policies
 # are never overwritten by a free-video update.
 [ -f /ffs/sbin/qconnstart ] && [ ! -L /ffs/sbin/qconnstart ] || exit 128
 if grep CNS275_REMOTE_ACCESS /ffs/sbin/qconnstart >/dev/null 2>&1;then
  grep CNS2_FREE_SSH_ONLY /ffs/sbin/qconnstart >/dev/null 2>&1 || exit 128
  [ -f /ffs/sbin/qconnstart.stock ] || exit 128
 fi
 for F in qconnstart_cns275_v24.sh remote_access_init_v24.sh remote_access_supervisor_v24.sh;do
  cp "$SRC/$F" "$ROOT/$F";chmod 700 "$ROOT/$F"
 done
 mount -uw /ffs
 trap 'mount -ur /ffs' 0
 if [ ! -e /ffs/sbin/qconnstart.stock ];then cp -p /ffs/sbin/qconnstart /ffs/sbin/qconnstart.stock;fi
 cp -p /ffs/sbin/qconnstart "$ROOT/backups/qconnstart.before-free-$G"
 cp "$SRC/qconnstart_cns275_v24.sh" /ffs/sbin/qconnstart.free-new
 chmod 755 /ffs/sbin/qconnstart.free-new
 /proc/boot/ksh -n /ffs/sbin/qconnstart.free-new
 mv /ffs/sbin/qconnstart.free-new /ffs/sbin/qconnstart
 mkdir -p /home/root
 touch /home/root/qconn_yes /home/root/ethernet_vag_yes
 mount -ur /ffs;trap - 0
 /proc/boot/ksh "$ROOT/qconnstart_cns275_v24.sh"
else
 # Reusing a prior paid SSH install requires its known persistent boot hook.
 grep CNS275_REMOTE_ACCESS_V24 /ffs/sbin/qconnstart >/dev/null || exit 129
 [ -f /home/root/qconn_yes ] && [ -x "$ROOT/remote_access_supervisor_v24.sh" ] || exit 129
fi
"$TOOL" --ssh-probe
"$TOOL" --digest "$SSH/etc/sshd_config" > "$ROOT/SSH_CONFIG_CHECKED-$G"
echo "$G $SERIAL" > "$ROOT/SSH_READY-$G"
cp "$SSH/etc/ssh_host_rsa_key.pub" "$USB/cns275_ssh_host_rsa_key.pub"
sync
echo 'SSH_READY: key-only configuration, authorized public key, exact daemon and TCP2222 banner checked'
