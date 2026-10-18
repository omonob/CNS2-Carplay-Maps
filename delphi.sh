#!/bin/sh
# Testmode engineering-menu entrypoint. Copy the package CONTENTS to USB root.
PATH=/proc/boot:/bin:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
USB=${1:-/usb1}
USB=${USB%/}
case "$USB" in /usb[0-9]|/fs/usb[0-9]) :;;*) echo 'Unsupported USB root';exit 1;;esac
exec /proc/boot/ksh "$USB/runtime/install.sh" "$USB"
