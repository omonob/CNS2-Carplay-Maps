#!/proc/boot/ksh
O=/ramdisk/.cns2-v43
"$O/cpmap-bootstrap.sh" </dev/null >> /ramdisk/.cns2-v43-state/bootstrap.log 2>&1 &
LD_PRELOAD=$O/libcns2-autoload-carplay-v9.so
export LD_PRELOAD
exec /extbin/apps/bin/CarPlay "$@"
