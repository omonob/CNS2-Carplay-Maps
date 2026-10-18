#!/proc/boot/ksh
# Starts only this exact known listener, never replaces or kills another one.
set -eu
ROOT=/persistence/cns275/sshd
export LD_LIBRARY_PATH=$ROOT/usr/lib:/extbin/apps/lib:/ifs2:/proc/boot
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
unset SOCK || :
mkdir -p "$ROOT/run"
if pidin ar | awk -v bin="$ROOT/usr/sbin/sshd" -v cfg="$ROOT/etc/sshd_config" '$2==bin && $3=="-f" && $4==cfg {f=1} END {exit f?0:1}';then exit 0;fi
"$ROOT/usr/sbin/sshd" -t -f "$ROOT/etc/sshd_config"
exec "$ROOT/usr/sbin/sshd" -f "$ROOT/etc/sshd_config"
