#!/system/bin/sh

TRACE=/sys/kernel/debug/tracing/
dev=$(((8<<20) + 17)) # sdb1 (8,17)

echo 1 > $TRACE/tracing_on

# mmc tracepoints
echo 0 > $TRACE/events/mmc/enable

# block tracepoints
#echo "dev == $dev" > $TRACE/events/block/block_rq_complete/filter
echo 0 > $TRACE/events/block/block_rq_complete/enable
echo 0 > $TRACE/events/block/block_bio_complete/enable

# GC
G=0
echo $G > $TRACE/events/f2fs/f2fs_get_victim/enable

# block allocation
A=0
echo $A > $TRACE/events/f2fs/f2fs_reserve_new_block/enable

# block truncation
T=0
echo $T > $TRACE/events/f2fs/f2fs_truncate/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_inode_blocks_enter/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_inode_blocks_exit/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_blocks_enter/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_blocks_exit/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_nodes_enter/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_nodes_exit/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_data_blocks_range/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_node/enable
echo $T > $TRACE/events/f2fs/f2fs_truncate_partial_nodes/enable

# syscalls
S=0
echo $S > $TRACE/events/f2fs/f2fs_unlink_enter/enable
echo $S > $TRACE/events/f2fs/f2fs_unlink_exit/enable
echo $S > $TRACE/events/f2fs/f2fs_fallocate/enable
echo $S > $TRACE/events/f2fs/f2fs_get_data_block/enable

# IOs
R=0
W=0
echo $R > $TRACE/events/f2fs/f2fs_readpage/enable
echo $W > $TRACE/events/f2fs/f2fs_writepage/enable
echo $W > $TRACE/events/f2fs/f2fs_write_begin/enable
echo $W > $TRACE/events/f2fs/f2fs_write_end/enable

echo 0 > $TRACE/events/f2fs/f2fs_submit_page_bio/enable
echo 0 > $TRACE/events/f2fs/f2fs_submit_page_mbio/enable
echo $R > $TRACE/events/f2fs/f2fs_submit_read_bio/enable
echo $W > $TRACE/events/f2fs/f2fs_submit_write_bio/enable

echo 0 > $TRACE/events/f2fs/f2fs_issue_discard/enable
echo 0 > $TRACE/events/f2fs/f2fs_issue_flush/enable

# VFS interfaces
V=0
echo $V > $TRACE/events/f2fs/f2fs_iget/enable
echo $V > $TRACE/events/f2fs/f2fs_iget_exit/enable
echo $V > $TRACE/events/f2fs/f2fs_new_inode/enable
echo $V > $TRACE/events/f2fs/f2fs_evict_inode/enable
echo $V > $TRACE/events/f2fs/f2fs_sync_file_enter/enable
echo $V > $TRACE/events/f2fs/f2fs_sync_file_exit/enable
echo $V > $TRACE/events/f2fs/f2fs_write_checkpoint/enable
echo $V > $TRACE/events/f2fs/f2fs_sync_fs/enable

cat $TRACE/trace_pipe
