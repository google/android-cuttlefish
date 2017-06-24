#pragma once
#warning "need to include it from drivers/staging/android/uapi/vsoc_shm.h"

#include <inttypes.h>

/*
 * shared memory layout based on
 * project: kernel/private/gce_x86
 * file: drivers/staging/android/uapi/vsoc_shm.h
 * commit-id: 2bc0d6b47c4a8d8204faecb7016af09a28e8c3ad
 *
 * TODO(romitd): We need to include the vsoc_shm.h file directly
 */
typedef struct {
  uint16_t major_version;
  uint16_t minor_version;
  /* size of the shm. This may be redundant but nice to
   * have.
   */
  uint32_t size;
  /* number of shared memory regions */
  uint32_t region_count;
  /* The offset to the start of region
   * descriptors
   */
  uint32_t vsoc_region_desc_offset;
} vsoc_shm_layout_descriptor;

/*
 * Describes a signal table in shared memory. Each non-zero entry in the
 * table indicates that the receiver should signal the futex at the given
 * offset. Offsets are relative to the region, not the shared memory window.
 */
typedef struct {
  // log_2(Number of signal table entries)
  uint32_t num_nodes_lg2;

  // Offset to the first signal table entry relative to the start
  // of the region.
  uint32_t offset;

  // Offset to an atomic uint32_t. Threads use this to get semi-unique access
  // to an entry in the table
  uint32_t node_alloc_hint_offset;

  // The doorbell number is implicitly assigned to the region number
} vsoc_signal_table_layout;

typedef struct {
  uint16_t current_version;
  uint16_t min_compatible_version;
  uint32_t region_begin_offset;
  uint32_t region_end_offset;
  uint32_t offset_of_region_data;
  vsoc_signal_table_layout guest_to_host_signal_table;
  vsoc_signal_table_layout host_to_guest_signal_table;
  /*
   * Name of the  device. Must always be terminated  with a  '\0', so
   * the longest supported device name is 15 characters.
   */
  char device_name[16];
} vsoc_device_region;
