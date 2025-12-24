#!/bin/bash

MNT=/mnt/f2fs
DEV=/dev/sdb1
USER_DIR=/home/zeus
F2FS_DIR=$USER_DIR/f2fs_test

check_stop() {
	stop=`cat /tmp/stop`
	if [ $stop -eq 1 ]; then
		exit
	fi
}

case $1 in
start)
	echo 0 > /tmp/stop
	umount /mnt/*
	echo 3 > /proc/sys/vm/drop_caches
	echo 8 > /proc/sys/kernel/printk

	date >> $USER_DIR/por_result
	sync

	insmod $F2FS_DIR/src/fs/f2fs/f2fs.ko || exit

	echo Start checking F2FS without fsync
	check_stop
	fsck.f2fs $DEV -d 0 || exit
	mount -t f2fs -o disable_roll_forward $DEV $MNT || exit
	umount $MNT
	echo 3 > /proc/sys/vm/drop_caches

	echo Start checking F2FS with fsync
	check_stop
	fsck.f2fs $DEV -d 0 || exit
	mount -t f2fs $DEV $MNT || exit
	umount $MNT

	check_stop
	fsck.f2fs $DEV -d 0 || exit
	mount -t f2fs $DEV $MNT || exit

	count=`cat $USER_DIR/por_time`
	if [ $count -eq 20 ]; then
		echo Start rm all
		time rm -rf $MNT/* || exit
		echo 0 > $USER_DIR/por_time
		sync
	else
		echo $((count+1)) > $USER_DIR/por_time
	fi
	echo 8 > /proc/sys/kernel/printk
	echo Start fsstress
	date
	$F2FS_DIR/stress_test/fsstress/fsstress -z -f link=0 -f mkdir=3 -f mknod=3 -f rmdir=2 -f symlink=3 -f truncate=4 -f write=10 -f creat=10 -f unlink=5 -f rename=5 -f fsync=10 -p 10 -n 10000 -l 0 -d $MNT &
	RANDOM=`date '+%s'`
	rand=$[($RANDOM % 540) + 60]
	echo Start sleep: $rand seconds
	sleep $rand

	echo Reboot now
	check_stop
	echo b > /proc/sysrq-trigger
	;;
stop)
	killall -9 fsstress
	echo 1 > /tmp/stop
	;;
esac
