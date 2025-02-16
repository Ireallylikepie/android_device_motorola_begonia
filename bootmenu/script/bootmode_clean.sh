#!/sbin/sh

#mount_ext3.sh cache /cache

export PATH=/sbin:/system/xbin:/system/bin

busybox mount -t yaffs2 -o nosuid,nodev,noatime,nodiratime,barrier=1 /dev/block/mtdblock8 /cache

mv /cache/recovery/bootmode.conf /cache/recovery/last_bootmode

