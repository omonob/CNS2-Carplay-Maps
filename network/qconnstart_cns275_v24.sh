#!/proc/boot/ksh
# CNS275_REMOTE_ACCESS_V24 CNS2_FREE_SSH_ONLY
# Historical boot-hook name: this script does NOT start qconn or Telnet.
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
ROOT=/persistence/cns275
if [ -x "$ROOT/remote_access_supervisor_v24.sh" ];then
 "$ROOT/remote_access_supervisor_v24.sh" </dev/null >> /tmp/cns2-free-network.log 2>&1 &
fi
exit 0
