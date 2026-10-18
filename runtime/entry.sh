#!/proc/boot/ksh
KIND=$1;shift
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
unset LD_PRELOAD CNS2_MOST_R41_ACTIVE CNS2_MOST_R42_ACTIVE CNS2_MOST_R43_ACTIVE CNS2_ENC_READY
# Stock HMI and NavInterface always run unmodified, without waiting for video.
case "$KIND" in
 hmi) exec /extbin/apps/hmi/HMI/runHMI.sh "$@";;
 nav) exec /extbin/apps/bin/NavInterface "$@";;
esac
G=
[ ! -L /persistence/.v43c/ACTIVE ] && read G < /persistence/.v43c/ACTIVE
case "$G" in ''|*[!a-f0-9]*) G=;;esac
if [ "${#G}" -eq 16 ] && /persistence/.v43c/$G/prepare.sh "$G";then
 O=/ramdisk/.cns2-v43
 case "$KIND" in
  carplay) exec "$O/CarPlay-cpmap-v2.sh" "$@";;
  most) LD_PRELOAD=$O/libcns2-most-pull-r43.so;CNS2_MOST_R43_ACTIVE=1
        export LD_PRELOAD CNS2_MOST_R43_ACTIVE;exec /extbin/apps/bin/Most "$@";;
 esac
fi
case "$KIND" in
 carplay) exec /extbin/apps/bin/CarPlay "$@";;
 most) exec /extbin/apps/bin/Most "$@";;
esac
exit 81
