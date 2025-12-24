#!/system/bin/sh
DEV=/dev/block/mmcblk0p16

CMD=$1
BASE=0x200000
BASE_MAIN=0xac00000

case $CMD in
cp1)
	echo dump cp1
	let addr=$BASE
	echo $addr
	hexdump -s $addr -n 4096 $DEV;;
cp2)
	echo dump cp2
	let addr=$BASE+0x200000
	hexdump -s $addr -n 4096 $DEV;;
cp)
	echo dump cp1 and cp2
	let addr=$BASE
	hexdump -s $addr -n 409 $DEV
	let addr=$BASE+0x200000
	hexdump -s $addr -n 4096 $DEV;;
cp1_all)
	echo dump cp1 all
	let addr=$BASE
	hexdump -s $addr -n 20480 $DEV;;
cp2_all)
	echo dump cp2 all
	let addr=$BASE+0x200000
	hexdump -s $addr -n 20480 $DEV;;
cp_all)
	echo dump cp1 and cp2 all
	let addr=$BASE
	hexdump -s $addr -n 20480 $DEV
	let addr=$BASE+0x200000
	hexdump -s $addr -n 20480 $DEV;;
blk)
	let addr=$BASE_MAIN+$2*0x200000+$3*0x1000
	hexdump -s $addr -n 4096 $DEV
	echo ;;
inode)
	let addr=$BASE_MAIN+$2*0x200000+$3*0x1000
	for i in `seq $3 511`
	do
		hexdump -s $addr -n 8 $DEV
		let end=$addr+0x0ff0
		hexdump -s $end -n 16 $DEV
		let addr=$addr+0x1000
	done
	echo ;;
*)
	let addr=$1*0x1000
	let segno=$addr-$BASE_MAIN
	let segno=$segno/0x200000
	let off=$addr-$BASE_MAIN
	let off=$off%0x200000/0x1000
	echo $segno, $off
	hexdump -s $addr -n 4096 $DEV
	echo ;;
esac
