#!/bin/bash

IMG=../test.img
TMP=/tmp/res
XFSTESTS=~/xfstests
TESTS="4 5 8 11 16 25 32 55 64"

TARGET=./testdir
MNT=/mnt/resize

mkdir $TARGET 2>/dev/null
mkdir $MNT 2>/dev/null

umount $TARGET
umount $MNT

_check_out()
{
	if [ $1 -ne 0 ]; then
		grep ASSERT $TMP
		echo FAIL RETURN $1
		exit
	fi
}

_get_sec()
{
	echo $(($1*1024*1024*1024/512))
}

_mkfs()
{
	echo "========== Initialize $1 GB ============"
	mkfs.f2fs $IMG `_get_sec $1` | grep sectors
}

_mount()
{
	echo "========== mount to $1 ================="
	mount -t f2fs -o loop,discard,inline_data,inline_xattr $IMG $1 2>&1
	_check_out $?
}

_fsck()
{
	echo "========== fsck.f2fs ==================="
	fsck.f2fs $IMG -t 2>&1 >$TMP
	_check_out $?
	grep FSCK $TMP
}

_fsstress()
{
	echo "========== fsstress $1 ================="
	$XFSTESTS/ltp/fsstress -x "echo 3 > /proc/sys/vm/drop_caches && sleep 1" -X 1 -r -f fsync=8 -f sync=0 -f write=8 -f dwrite=2 -f truncate=6 -f allocsp=0 -f bulkstat=0 -f bulkstat1=0 -f freesp=0 -f zero=1 -f collapse=1 -f insert=1 -f resvsp=0 -f unresvsp=0 -S t -p 10 -n $2 -d $1 >/dev/null
}

_resize()
{
	echo "========== resize.f2fs $1 GB ==========="
	resize.f2fs -t `_get_sec $1` $IMG 2>&1 >$TMP
	_check_out $?
	_fsck
}

_resize_tests()
{
	for i in $TESTS
	do
		if [ $i -ge $1 ]; then
			_resize $i
		fi
	done
}

_sload()
{
	echo "========== sload $1 ===================="
	sload.f2fs -f $1 $IMG 2>&1
	_check_out $?
}

from_mount()
{
	echo ""
	echo " ****  $1 GB to $2 GB with $3 *** "
	_mkfs $1
	_mount $3
	_fsstress $3 10000
	umount $3
	_fsck
	_resize_tests $2
}

from_sload()
{
	echo ""
	echo " ****  $1 GB to $2 GB with $3 *** "

	_mkfs $1
	_sload $3
	_fsck

	_mount $MNT
	_fsstress $MNT 10000
	umount $MNT
	_fsck

	_resize_tests $2

	_mount $MNT
	_fsstress $MNT 10000
	umount $MNT
	_fsck
}

test_all()
{
	for i in $TESTS
	do
		for j in $TESTS
		do
			if [ $i -lt $j ]; then
				$1 $i $j $2
			fi
		done
	done
}

test_all from_sload ~/grub

rm -rf $TARGET/*
_fsstress $TARGET 5000
test_all from_sload $TARGET
rm -rf $TARGET 2>/dev/null

test_all from_mount $MNT
