#!/usr/bin/env python

# Copyright 2016, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


"""Command-line tool for partitioning Brillo images."""


import argparse
import bisect
import copy
import json
import math
import numbers
import os
import struct
import sys
import uuid
import zlib

# Python 2.6 required for modern exception syntax
if sys.hexversion < 0x02060000:
  print >> sys.stderr, "Python 2.6 or newer is required."
  sys.exit(1)

# Keywords used in JSON files.
JSON_KEYWORD_SETTINGS = 'settings'
JSON_KEYWORD_SETTINGS_AB_SUFFIXES = 'ab_suffixes'
JSON_KEYWORD_SETTINGS_DISK_SIZE = 'disk_size'
JSON_KEYWORD_SETTINGS_DISK_ALIGNMENT = 'disk_alignment'
JSON_KEYWORD_SETTINGS_DISK_GUID = 'disk_guid'
JSON_KEYWORD_SETTINGS_PARTITIONS_OFFSET_BEGIN = 'partitions_offset_begin'
JSON_KEYWORD_PARTITIONS = 'partitions'
JSON_KEYWORD_PARTITIONS_LABEL = 'label'
JSON_KEYWORD_PARTITIONS_OFFSET = 'offset'
JSON_KEYWORD_PARTITIONS_SIZE = 'size'
JSON_KEYWORD_PARTITIONS_GROW = 'grow'
JSON_KEYWORD_PARTITIONS_GUID = 'guid'
JSON_KEYWORD_PARTITIONS_TYPE_GUID = 'type_guid'
JSON_KEYWORD_PARTITIONS_FLAGS = 'flags'
JSON_KEYWORD_PARTITIONS_PERSIST = 'persist'
JSON_KEYWORD_PARTITIONS_IGNORE = 'ignore'
JSON_KEYWORD_PARTITIONS_AB = 'ab'
JSON_KEYWORD_PARTITIONS_AB_EXPANDED = 'ab_expanded'
JSON_KEYWORD_PARTITIONS_POSITION = 'position'
JSON_KEYWORD_AUTO = 'auto'

# Possible values for the --type option of the query_partition
# sub-command.
QUERY_PARTITION_TYPES = ['size',
                         'offset',
                         'guid',
                         'type_guid',
                         'flags',
                         'persist']

BPT_VERSION_MAJOR = 1
BPT_VERSION_MINOR = 0

DISK_SECTOR_SIZE = 512

GPT_NUM_LBAS = 33

GPT_MIN_PART_NUM = 1
GPT_MAX_PART_NUM = 128

KNOWN_TYPE_GUIDS = {
    'brillo_boot': 'bb499290-b57e-49f6-bf41-190386693794',
    'brillo_bootloader': '4892aeb3-a45f-4c5f-875f-da3303c0795c',
    'brillo_system': '0f2778c4-5cc1-4300-8670-6c88b7e57ed6',
    'brillo_odm': 'e99d84d7-2c1b-44cf-8c58-effae2dc2558',
    'brillo_oem': 'aa3434b2-ddc3-4065-8b1a-18e99ea15cb7',
    'brillo_userdata': '0bb7e6ed-4424-49c0-9372-7fbab465ab4c',
    'brillo_misc': '6b2378b0-0fbc-4aa9-a4f6-4d6e17281c47',
    'brillo_vbmeta': 'b598858a-5fe3-418e-b8c4-824b41f4adfc',
    'brillo_vendor_specific': '314f99d5-b2bf-4883-8d03-e2f2ce507d6a',
    'linux_fs': '0fc63daf-8483-4772-8e79-3d69d8477de4',
    'ms_basic_data': 'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7',
    'efi_system': 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
}


def RoundToMultiple(number, size, round_down=False):
  """Rounds a number up (or down) to nearest multiple of another number.

  Args:
    number: The number to round up.
    size: The multiple to round up to.
    round_down: If True, the number will be rounded down.

  Returns:
    If |number| is a multiple of |size|, returns |number|, otherwise
    returns |number| + |size| - |remainder| (if |round_down| is False) or
    |number| - |remainder| (if |round_down| is True). Always returns
    an integer.
  """
  remainder = number % size
  if remainder == 0:
    return int(number)
  if round_down:
    return int(number - remainder)
  return int(number + size - remainder)


def ParseNumber(arg):
  """Number parser.

  If |arg| is an integer, that value is returned. Otherwise int(arg, 0)
  is returned.

  This function is suitable for use in the |type| parameter of
  |ArgumentParser|'s add_argument() function. An improvement to just
  using type=int is that this function supports numbers in other
  bases, e.g. "0x1234".

  Arguments:
    arg: Argument (int or string) to parse.

  Returns:
    The parsed value, as an integer.

  Raises:
    ValueError: If the argument could not be parsed.
  """
  if isinstance(arg, numbers.Integral):
    return arg
  return int(arg, 0)


def ParseGuid(arg):
  """Parser for RFC 4122 GUIDs.

  Arguments:
    arg: The argument, as a string.

  Returns:
    UUID in hyphenated format.

  Raises:
    ValueError: If the given string cannot be parsed.
  """
  return str(uuid.UUID(arg))


def ParseSize(arg):
  """Parser for size strings with decimal and binary unit support.

  This supports both integers and strings.

  Arguments:
    arg: The string to parse.

  Returns:
    The parsed size in bytes as an integer.

  Raises:
    ValueError: If the given string cannot be parsed.
  """
  if isinstance(arg, numbers.Integral):
    return arg

  ws_index = arg.find(' ')
  if ws_index != -1:
    num = float(arg[0:ws_index])
    factor = 1
    if arg.endswith('KiB'):
      factor = 1024
    elif arg.endswith('MiB'):
      factor = 1024*1024
    elif arg.endswith('GiB'):
      factor = 1024*1024*1024
    elif arg.endswith('TiB'):
      factor = 1024*1024*1024*1024
    elif arg.endswith('PiB'):
      factor = 1024*1024*1024*1024*1024
    elif arg.endswith('kB'):
      factor = 1000
    elif arg.endswith('MB'):
      factor = 1000*1000
    elif arg.endswith('GB'):
      factor = 1000*1000*1000
    elif arg.endswith('TB'):
      factor = 1000*1000*1000*1000
    elif arg.endswith('PB'):
      factor = 1000*1000*1000*1000*1000
    else:
      raise ValueError('Cannot parse string "{}"'.format(arg))
    value = num*factor
    # If the resulting value isn't an integer, round up.
    if not value.is_integer():
      value = int(math.ceil(value))
  else:
    value = int(arg, 0)
  return value


class ImageChunk(object):
  """Data structure used for representing chunks in Android sparse files.

  Attributes:
    chunk_type: One of TYPE_RAW, TYPE_FILL, or TYPE_DONT_CARE.
    chunk_offset: Offset in the sparse file where this chunk begins.
    output_offset: Offset in de-sparsified file where output begins.
    output_size: Number of bytes in output.
    input_offset: Offset in sparse file for data if TYPE_RAW otherwise None.
    fill_data: Blob with data to fill if TYPE_FILL otherwise None.
  """

  FORMAT = '<2H2I'
  TYPE_RAW = 0xcac1
  TYPE_FILL = 0xcac2
  TYPE_DONT_CARE = 0xcac3
  TYPE_CRC32 = 0xcac4

  def __init__(self, chunk_type, chunk_offset, output_offset, output_size,
               input_offset, fill_data):
    """Initializes an ImageChunk object.

    Arguments:
      chunk_type: One of TYPE_RAW, TYPE_FILL, or TYPE_DONT_CARE.
      chunk_offset: Offset in the sparse file where this chunk begins.
      output_offset: Offset in de-sparsified file.
      output_size: Number of bytes in output.
      input_offset: Offset in sparse file if TYPE_RAW otherwise None.
      fill_data: Blob with data to fill if TYPE_FILL otherwise None.

    Raises:
      ValueError: If data is not well-formed.
    """
    self.chunk_type = chunk_type
    self.chunk_offset = chunk_offset
    self.output_offset = output_offset
    self.output_size = output_size
    self.input_offset = input_offset
    self.fill_data = fill_data
    # Check invariants.
    if self.chunk_type == self.TYPE_RAW:
      if self.fill_data is not None:
        raise ValueError('RAW chunk cannot have fill_data set.')
      if not self.input_offset:
        raise ValueError('RAW chunk must have input_offset set.')
    elif self.chunk_type == self.TYPE_FILL:
      if self.fill_data is None:
        raise ValueError('FILL chunk must have fill_data set.')
      if self.input_offset:
        raise ValueError('FILL chunk cannot have input_offset set.')
    elif self.chunk_type == self.TYPE_DONT_CARE:
      if self.fill_data is not None:
        raise ValueError('DONT_CARE chunk cannot have fill_data set.')
      if self.input_offset:
        raise ValueError('DONT_CARE chunk cannot have input_offset set.')
    else:
      raise ValueError('Invalid chunk type')


class ImageHandler(object):
  """Abstraction for image I/O with support for Android sparse images.

  This class provides an interface for working with image files that
  may be using the Android Sparse Image format. When an instance is
  constructed, we test whether it's an Android sparse file. If so,
  operations will be on the sparse file by interpreting the sparse
  format, otherwise they will be directly on the file. Either way the
  operations do the same.

  For reading, this interface mimics a file object - it has seek(),
  tell(), and read() methods. For writing, only truncation
  (truncate()) and appending is supported (append_raw(),
  append_fill(), and append_dont_care()). Additionally, data can only
  be written in units of the block size.

  Attributes:
    is_sparse: Whether the file being operated on is sparse.
    block_size: The block size, typically 4096.
    image_size: The size of the unsparsified file.

  """
  # See system/core/libsparse/sparse_format.h for details.
  MAGIC = 0xed26ff3a
  HEADER_FORMAT = '<I4H4I'

  # These are formats and offset of just the |total_chunks| and
  # |total_blocks| fields.
  NUM_CHUNKS_AND_BLOCKS_FORMAT = '<II'
  NUM_CHUNKS_AND_BLOCKS_OFFSET = 16

  def __init__(self, image_filename):
    """Initializes an image handler.

    Arguments:
      image_filename: The name of the file to operate on.

    Raises:
      ValueError: If data in the file is invalid.
    """
    self._image_filename = image_filename
    self._read_header()

  def _read_header(self):
    """Initializes internal data structures used for reading file.

    This may be called multiple times and is typically called after
    modifying the file (e.g. appending, truncation).

    Raises:
      ValueError: If data in the file is invalid.
    """
    self.is_sparse = False
    self.block_size = 4096
    self._file_pos = 0
    self._image = open(self._image_filename, 'r+b')
    self._image.seek(0, os.SEEK_END)
    self.image_size = self._image.tell()

    self._image.seek(0, os.SEEK_SET)
    header_bin = self._image.read(struct.calcsize(self.HEADER_FORMAT))
    if len(header_bin) < struct.calcsize(self.HEADER_FORMAT):
      # Not a sparse image, our job here is done.
      return
    (magic, major_version, minor_version, file_hdr_sz, chunk_hdr_sz,
     block_size, self._num_total_blocks, self._num_total_chunks,
     _) = struct.unpack(self.HEADER_FORMAT, header_bin)
    if magic != self.MAGIC:
      # Not a sparse image, our job here is done.
      return
    if not (major_version == 1 and minor_version == 0):
      raise ValueError('Encountered sparse image format version {}.{} but '
                       'only 1.0 is supported'.format(major_version,
                                                      minor_version))
    if file_hdr_sz != struct.calcsize(self.HEADER_FORMAT):
      raise ValueError('Unexpected file_hdr_sz value {}.'.
                       format(file_hdr_sz))
    if chunk_hdr_sz != struct.calcsize(ImageChunk.FORMAT):
      raise ValueError('Unexpected chunk_hdr_sz value {}.'.
                       format(chunk_hdr_sz))

    self.block_size = block_size

    # Build an list of chunks by parsing the file.
    self._chunks = []

    # Find the smallest offset where only "Don't care" chunks
    # follow. This will be the size of the content in the sparse
    # image.
    offset = 0
    output_offset = 0
    for _ in xrange(1, self._num_total_chunks + 1):
      chunk_offset = self._image.tell()

      header_bin = self._image.read(struct.calcsize(ImageChunk.FORMAT))
      (chunk_type, _, chunk_sz, total_sz) = struct.unpack(ImageChunk.FORMAT,
                                                          header_bin)
      data_sz = total_sz - struct.calcsize(ImageChunk.FORMAT)

      if chunk_type == ImageChunk.TYPE_RAW:
        if data_sz != (chunk_sz * self.block_size):
          raise ValueError('Raw chunk input size ({}) does not match output '
                           'size ({})'.
                           format(data_sz, chunk_sz*self.block_size))
        self._chunks.append(ImageChunk(ImageChunk.TYPE_RAW,
                                       chunk_offset,
                                       output_offset,
                                       chunk_sz*self.block_size,
                                       self._image.tell(),
                                       None))
        self._image.read(data_sz)

      elif chunk_type == ImageChunk.TYPE_FILL:
        if data_sz != 4:
          raise ValueError('Fill chunk should have 4 bytes of fill, but this '
                           'has {}'.format(data_sz))
        fill_data = self._image.read(4)
        self._chunks.append(ImageChunk(ImageChunk.TYPE_FILL,
                                       chunk_offset,
                                       output_offset,
                                       chunk_sz*self.block_size,
                                       None,
                                       fill_data))
      elif chunk_type == ImageChunk.TYPE_DONT_CARE:
        if data_sz != 0:
          raise ValueError('Don\'t care chunk input size is non-zero ({})'.
                           format(data_sz))
        self._chunks.append(ImageChunk(ImageChunk.TYPE_DONT_CARE,
                                       chunk_offset,
                                       output_offset,
                                       chunk_sz*self.block_size,
                                       None,
                                       None))
      elif chunk_type == ImageChunk.TYPE_CRC32:
        if data_sz != 4:
          raise ValueError('CRC32 chunk should have 4 bytes of CRC, but '
                           'this has {}'.format(data_sz))
        self._image.read(4)
      else:
        raise ValueError('Unknown chunk type {}'.format(chunk_type))

      offset += chunk_sz
      output_offset += chunk_sz * self.block_size

    # Record where sparse data end.
    self._sparse_end = self._image.tell()

    # Now that we've traversed all chunks, sanity check.
    if self._num_total_blocks != offset:
      raise ValueError('The header said we should have {} output blocks, '
                       'but we saw {}'.format(self._num_total_blocks, offset))
    junk_len = len(self._image.read())
    if junk_len > 0:
      raise ValueError('There were {} bytes of extra data at the end of the '
                       'file.'.format(junk_len))

    # Assign |image_size|.
    self.image_size = output_offset

    # This is used when bisecting in read() to find the initial slice.
    self._chunk_output_offsets = [i.output_offset for i in self._chunks]

    self.is_sparse = True

  def _update_chunks_and_blocks(self):
    """Helper function to update the image header.

    The the |total_chunks| and |total_blocks| fields in the header
    will be set to value of the |_num_total_blocks| and
    |_num_total_chunks| attributes.

    """
    self._image.seek(self.NUM_CHUNKS_AND_BLOCKS_OFFSET, os.SEEK_SET)
    self._image.write(struct.pack(self.NUM_CHUNKS_AND_BLOCKS_FORMAT,
                                  self._num_total_blocks,
                                  self._num_total_chunks))

  def append_dont_care(self, num_bytes):
    """Appends a DONT_CARE chunk to the sparse file.

    The given number of bytes must be a multiple of the block size.

    Arguments:
      num_bytes: Size in number of bytes of the DONT_CARE chunk.
    """
    assert num_bytes % self.block_size == 0

    if not self.is_sparse:
      self._image.seek(0, os.SEEK_END)
      # This is more efficient that writing NUL bytes since it'll add
      # a hole on file systems that support sparse files (native
      # sparse, not Android sparse).
      self._image.truncate(self._image.tell() + num_bytes)
      self._read_header()
      return

    self._num_total_chunks += 1
    self._num_total_blocks += num_bytes / self.block_size
    self._update_chunks_and_blocks()

    self._image.seek(self._sparse_end, os.SEEK_SET)
    self._image.write(struct.pack(ImageChunk.FORMAT,
                                  ImageChunk.TYPE_DONT_CARE,
                                  0,  # Reserved
                                  num_bytes / self.block_size,
                                  struct.calcsize(ImageChunk.FORMAT)))
    self._read_header()

  def append_raw(self, data):
    """Appends a RAW chunk to the sparse file.

    The length of the given data must be a multiple of the block size.

    Arguments:
      data: Data to append.
    """
    assert len(data) % self.block_size == 0

    if not self.is_sparse:
      self._image.seek(0, os.SEEK_END)
      self._image.write(data)
      self._read_header()
      return

    self._num_total_chunks += 1
    self._num_total_blocks += len(data) / self.block_size
    self._update_chunks_and_blocks()

    self._image.seek(self._sparse_end, os.SEEK_SET)
    self._image.write(struct.pack(ImageChunk.FORMAT,
                                  ImageChunk.TYPE_RAW,
                                  0,  # Reserved
                                  len(data) / self.block_size,
                                  len(data) +
                                  struct.calcsize(ImageChunk.FORMAT)))
    self._image.write(data)
    self._read_header()

  def append_fill(self, fill_data, size):
    """Appends a fill chunk to the sparse file.

    The total length of the fill data must be a multiple of the block size.

    Arguments:
      fill_data: Fill data to append - must be four bytes.
      size: Number of chunk - must be a multiple of four and the block size.
    """
    assert len(fill_data) == 4
    assert size % 4 == 0
    assert size % self.block_size == 0

    if not self.is_sparse:
      self._image.seek(0, os.SEEK_END)
      self._image.write(fill_data * (size/4))
      self._read_header()
      return

    self._num_total_chunks += 1
    self._num_total_blocks += size / self.block_size
    self._update_chunks_and_blocks()

    self._image.seek(self._sparse_end, os.SEEK_SET)
    self._image.write(struct.pack(ImageChunk.FORMAT,
                                  ImageChunk.TYPE_FILL,
                                  0,  # Reserved
                                  size / self.block_size,
                                  4 + struct.calcsize(ImageChunk.FORMAT)))
    self._image.write(fill_data)
    self._read_header()

  def seek(self, offset):
    """Sets the cursor position for reading from unsparsified file.

    Arguments:
      offset: Offset to seek to from the beginning of the file.
    """
    self._file_pos = offset

  def read(self, size):
    """Reads data from the unsparsified file.

    This method may return fewer than |size| bytes of data if the end
    of the file was encountered.

    The file cursor for reading is advanced by the number of bytes
    read.

    Arguments:
      size: Number of bytes to read.

    Returns:
      The data.

    """
    if not self.is_sparse:
      self._image.seek(self._file_pos)
      data = self._image.read(size)
      self._file_pos += len(data)
      return data

    # Iterate over all chunks.
    chunk_idx = bisect.bisect_right(self._chunk_output_offsets,
                                    self._file_pos) - 1
    data = bytearray()
    to_go = size
    while to_go > 0:
      chunk = self._chunks[chunk_idx]
      chunk_pos_offset = self._file_pos - chunk.output_offset
      chunk_pos_to_go = min(chunk.output_size - chunk_pos_offset, to_go)

      if chunk.chunk_type == ImageChunk.TYPE_RAW:
        self._image.seek(chunk.input_offset + chunk_pos_offset)
        data.extend(self._image.read(chunk_pos_to_go))
      elif chunk.chunk_type == ImageChunk.TYPE_FILL:
        all_data = chunk.fill_data*(chunk_pos_to_go/len(chunk.fill_data) + 2)
        offset_mod = chunk_pos_offset % len(chunk.fill_data)
        data.extend(all_data[offset_mod:(offset_mod + chunk_pos_to_go)])
      else:
        assert chunk.chunk_type == ImageChunk.TYPE_DONT_CARE
        data.extend('\0' * chunk_pos_to_go)

      to_go -= chunk_pos_to_go
      self._file_pos += chunk_pos_to_go
      chunk_idx += 1
      # Generate partial read in case of EOF.
      if chunk_idx >= len(self._chunks):
        break

    return data

  def tell(self):
    """Returns the file cursor position for reading from unsparsified file.

    Returns:
      The file cursor position for reading.
    """
    return self._file_pos

  def truncate(self, size):
    """Truncates the unsparsified file.

    Arguments:
      size: Desired size of unsparsified file.

    Raises:
      ValueError: If desired size isn't a multiple of the block size.
    """
    if not self.is_sparse:
      self._image.truncate(size)
      self._read_header()
      return

    if size % self.block_size != 0:
      raise ValueError('Cannot truncate to a size which is not a multiple '
                       'of the block size')

    if size == self.image_size:
      # Trivial where there's nothing to do.
      return
    elif size < self.image_size:
      chunk_idx = bisect.bisect_right(self._chunk_output_offsets, size) - 1
      chunk = self._chunks[chunk_idx]
      if chunk.output_offset != size:
        # Truncation in the middle of a trunk - need to keep the chunk
        # and modify it.
        chunk_idx_for_update = chunk_idx + 1
        num_to_keep = size - chunk.output_offset
        assert num_to_keep % self.block_size == 0
        if chunk.chunk_type == ImageChunk.TYPE_RAW:
          truncate_at = (chunk.chunk_offset +
                         struct.calcsize(ImageChunk.FORMAT) + num_to_keep)
          data_sz = num_to_keep
        elif chunk.chunk_type == ImageChunk.TYPE_FILL:
          truncate_at = (chunk.chunk_offset +
                         struct.calcsize(ImageChunk.FORMAT) + 4)
          data_sz = 4
        else:
          assert chunk.chunk_type == ImageChunk.TYPE_DONT_CARE
          truncate_at = chunk.chunk_offset + struct.calcsize(ImageChunk.FORMAT)
          data_sz = 0
        chunk_sz = num_to_keep/self.block_size
        total_sz = data_sz + struct.calcsize(ImageChunk.FORMAT)
        self._image.seek(chunk.chunk_offset)
        self._image.write(struct.pack(ImageChunk.FORMAT,
                                      chunk.chunk_type,
                                      0,  # Reserved
                                      chunk_sz,
                                      total_sz))
        chunk.output_size = num_to_keep
      else:
        # Truncation at trunk boundary.
        truncate_at = chunk.chunk_offset
        chunk_idx_for_update = chunk_idx

      self._num_total_chunks = chunk_idx_for_update
      self._num_total_blocks = 0
      for i in range(0, chunk_idx_for_update):
        self._num_total_blocks += self._chunks[i].output_size / self.block_size
      self._update_chunks_and_blocks()
      self._image.truncate(truncate_at)

      # We've modified the file so re-read all data.
      self._read_header()
    else:
      # Truncating to grow - just add a DONT_CARE section.
      self.append_dont_care(size - self.image_size)


class GuidGenerator(object):
  """An interface for obtaining strings that are GUIDs.

  To facilitate unit testing, this abstraction is used instead of the
  directly using the uuid module.
  """

  def dispense_guid(self, partition_number):
    """Dispenses a GUID.

    Arguments:
      partition_number: The partition number or 0 if requesting a GUID
                        for the whole disk.

    Returns:
      A RFC 4122 compliant GUID, as a string.
    """
    return str(uuid.uuid4())


class Partition(object):
  """Object representing a partition.

  Attributes:
    label: The partition label.
    offset: Offset of the partition on the disk, or None.
    size: Size of the partition or None if not specified.
    grow: True if partition has been requested to use all remaining space.
    guid: Instance GUID (RFC 4122 compliant) as a string or None or 'auto'
          if it should be automatically generated.
    type_guid: Type GUID (RFC 4122 compliant) as a string or a known type
               from the |KNOWN_TYPE_GUIDS| map.
    flags: GUID flags.
    persist: If true, sets bit 0 of flags indicating that this partition should
             not be deleted by the bootloader.
    ab: If True, the partition is an A/B partition.
    ab_expanded: If True, the A/B partitions have been generated.
    ignore: If True, the partition should not be included in the final output.
    position: The requested position of the partition or 0 if it doesn't matter.
  """

  def __init__(self):
    """Initializer method."""
    self.label = ''
    self.offset = None
    self.size = None
    self.grow = False
    self.guid = None
    self.type_guid = None
    self.flags = 0
    self.persist = False
    self.ab = False
    self.ab_expanded = False
    self.ignore = False
    self.position = 0

  def add_info(self, pobj):
    """Add information to partition.

    Arguments:
      pobj: A JSON object with information about the partition.
    """
    self.label = pobj[JSON_KEYWORD_PARTITIONS_LABEL]
    value = pobj.get(JSON_KEYWORD_PARTITIONS_OFFSET)
    if value is not None:
      self.offset = ParseSize(value)
    value = pobj.get(JSON_KEYWORD_PARTITIONS_SIZE)
    if value is not None:
      self.size = ParseSize(value)
    value = pobj.get(JSON_KEYWORD_PARTITIONS_GROW)
    if value is not None:
      self.grow = value
    value = pobj.get(JSON_KEYWORD_PARTITIONS_AB)
    if value is not None:
      self.ab = value
    value = pobj.get(JSON_KEYWORD_PARTITIONS_AB_EXPANDED)
    if value is not None:
      self.ab_expanded = value
    value = pobj.get(JSON_KEYWORD_PARTITIONS_GUID)
    if value is not None:
      self.guid = value
    value = pobj.get(JSON_KEYWORD_PARTITIONS_IGNORE)
    if value is not None:
      self.ignore = value
    value = pobj.get(JSON_KEYWORD_PARTITIONS_TYPE_GUID)
    if value is not None:
      self.type_guid = str.lower(str(value))
      if self.type_guid in KNOWN_TYPE_GUIDS:
        self.type_guid = KNOWN_TYPE_GUIDS[self.type_guid]
    value = pobj.get(JSON_KEYWORD_PARTITIONS_FLAGS)
    if value is not None:
      self.flags = ParseNumber(value)
    value = pobj.get(JSON_KEYWORD_PARTITIONS_PERSIST)
    if value is not None:
      self.persist = value
      if value:
        self.flags = self.flags | 0x1
    value = pobj.get(JSON_KEYWORD_PARTITIONS_POSITION)
    if value is not None:
      self.position = ParseNumber(value)

  def expand_guid(self, guid_generator, partition_number):
    """Assign instance GUID and type GUID if required.

    Arguments:
      guid_generator: A GuidGenerator object.
      partition_number: The partition number, starting from 1.
    """
    if not self.guid or self.guid == JSON_KEYWORD_AUTO:
      self.guid = guid_generator.dispense_guid(partition_number)
    if not self.type_guid:
      self.type_guid = KNOWN_TYPE_GUIDS['brillo_vendor_specific']

  def validate(self):
    """Sanity checks data in object."""

    try:
      _ = uuid.UUID(str(self.guid))
    except ValueError:
      raise ValueError('The string "{}" is not a valid GPT instance GUID on '
                       'partition with label "{}".'.format(
                           str(self.guid), self.label))

    try:
      _ = uuid.UUID(str(self.type_guid))
    except ValueError:
      raise ValueError('The string "{}" is not a valid GPT type GUID on '
                       'partition with label "{}".'.format(
                           str(self.type_guid), self.label))

    if not self.size:
      if not self.grow:
        raise ValueError('Size can only be unset if "grow" is True.')

  def cmp(self, other):
    """Comparison method."""
    self_position = self.position
    if self_position == 0:
      self_position = GPT_MAX_PART_NUM
    other_position = other.position
    if other_position == 0:
      other_position = GPT_MAX_PART_NUM
    return cmp(self_position, other_position)


class Settings(object):
  """An object for holding settings.

  Attributes:
    ab_suffixes: A list of A/B suffixes to use.
    disk_size: An integer with the disk size in bytes.
    partitions_offset_begin: An integer with the disk partitions
                             offset begin size in bytes.
    disk_alignment: The alignment to use for partitions.
    disk_guid: The GUID to use for the disk or None or 'auto' if
               automatically generated.
  """

  def __init__(self):
    """Initializer with defaults."""
    self.ab_suffixes = ['_a', '_b']
    self.disk_size = None
    self.partitions_offset_begin = 0
    self.disk_alignment = 4096
    self.disk_guid = JSON_KEYWORD_AUTO


class BptError(Exception):
  """Application-specific errors.

  These errors represent issues for which a stack-trace should not be
  presented.

  Attributes:
    message: Error message.
  """

  def __init__(self, message):
    Exception.__init__(self, message)


class BptParsingError(BptError):
  """Represents an error with an input file.

  Attributes:
    message: Error message.
    filename: Name of the file that caused an error.
  """

  def __init__(self, filename, message):
    self.filename = filename
    BptError.__init__(self, message)


class Bpt(object):
  """Business logic for bpttool command-line tool."""

  def _read_json(self, input_files, ab_collapse=True):
    """Parses a stack of JSON files into suitable data structures.

    The order of files matters as later files can modify partitions
    declared in earlier files.

    Arguments:
      input_files: An ordered list of open files.
      ab_collapse: If True, collapse A/B partitions.

    Returns:
      A tuple where the first element is a list of Partition objects
      and the second element is a Settings object.

    Raises:
      BptParsingError: If an input file has an error.
    """
    partitions = []
    settings = Settings()

    # Read all input file and merge partitions and settings.
    for f in input_files:
      try:
        obj = json.loads(f.read())
      except ValueError as e:
        # Unfortunately we can't easily get the line number where the
        # error occurred.
        raise BptParsingError(f.name, e.message)

      sobj = obj.get(JSON_KEYWORD_SETTINGS)
      if sobj:
        ab_suffixes = sobj.get(JSON_KEYWORD_SETTINGS_AB_SUFFIXES)
        if ab_suffixes:
          settings.ab_suffixes = ab_suffixes
        disk_size = sobj.get(JSON_KEYWORD_SETTINGS_DISK_SIZE)
        if disk_size:
          settings.disk_size = ParseSize(disk_size)
        partitions_offset_begin = sobj.get(
                JSON_KEYWORD_SETTINGS_PARTITIONS_OFFSET_BEGIN)
        if partitions_offset_begin:
          settings.partitions_offset_begin = ParseSize(partitions_offset_begin)
        disk_alignment = sobj.get(JSON_KEYWORD_SETTINGS_DISK_ALIGNMENT)
        if disk_alignment:
          settings.disk_alignment = ParseSize(disk_alignment)
        disk_guid = sobj.get(JSON_KEYWORD_SETTINGS_DISK_GUID)
        if disk_guid:
          settings.disk_guid = disk_guid

      pobjs = obj.get(JSON_KEYWORD_PARTITIONS)
      if pobjs:
        for pobj in pobjs:
          if ab_collapse and pobj.get(JSON_KEYWORD_PARTITIONS_AB_EXPANDED):
            # If we encounter an expanded partition, unexpand it. This
            # is to make it possible to use output-JSON (from this tool)
            # and stack it with an input-JSON file that e.g. specifies
            # size='256 GiB' for the 'system' partition.
            label = pobj[JSON_KEYWORD_PARTITIONS_LABEL]
            if label.endswith(settings.ab_suffixes[0]):
              # Modify first A/B copy so it doesn't have the trailing suffix.
              new_len = len(label) - len(settings.ab_suffixes[0])
              pobj[JSON_KEYWORD_PARTITIONS_LABEL] = label[0:new_len]
              pobj[JSON_KEYWORD_PARTITIONS_AB_EXPANDED] = False
              pobj[JSON_KEYWORD_PARTITIONS_GUID] = JSON_KEYWORD_AUTO
            else:
              # Skip other A/B copies.
              continue
          # Find or create a partition.
          p = None
          for candidate in partitions:
            if candidate.label == pobj[JSON_KEYWORD_PARTITIONS_LABEL]:
              p = candidate
              break
          if not p:
            p = Partition()
            partitions.append(p)
          p.add_info(pobj)

    return partitions, settings

  def _generate_json(self, partitions, settings):
    """Generate a string with JSON representing partitions and settings.

    Arguments:
      partitions: A list of Partition objects.
      settings: A Settings object.

    Returns:
      A JSON string.
    """
    suffixes_str = '['
    for n in range(0, len(settings.ab_suffixes)):
      if n != 0:
        suffixes_str += ', '
      suffixes_str += '"{}"'.format(settings.ab_suffixes[n])
    suffixes_str += ']'

    ret = ('{{\n'
           '  "' + JSON_KEYWORD_SETTINGS + '": {{\n'
           '    "' + JSON_KEYWORD_SETTINGS_AB_SUFFIXES + '": {},\n'
           '    "' + JSON_KEYWORD_SETTINGS_PARTITIONS_OFFSET_BEGIN + '": {},\n'
           '    "' + JSON_KEYWORD_SETTINGS_DISK_SIZE + '": {},\n'
           '    "' + JSON_KEYWORD_SETTINGS_DISK_ALIGNMENT + '": {},\n'
           '    "' + JSON_KEYWORD_SETTINGS_DISK_GUID + '": "{}"\n'
           '  }},\n'
           '  "' + JSON_KEYWORD_PARTITIONS + '": [\n').format(
               suffixes_str,
               settings.partitions_offset_begin,
               settings.disk_size,
               settings.disk_alignment,
               settings.disk_guid)

    for n in range(0, len(partitions)):
      p = partitions[n]
      ret += ('    {{\n'
              '      "' + JSON_KEYWORD_PARTITIONS_LABEL + '": "{}",\n'
              '      "' + JSON_KEYWORD_PARTITIONS_OFFSET + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_SIZE + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_GROW + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_GUID + '": "{}",\n'
              '      "' + JSON_KEYWORD_PARTITIONS_TYPE_GUID + '": "{}",\n'
              '      "' + JSON_KEYWORD_PARTITIONS_FLAGS + '": "{:#018x}",\n'
              '      "' + JSON_KEYWORD_PARTITIONS_PERSIST + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_IGNORE + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_AB + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_AB_EXPANDED + '": {},\n'
              '      "' + JSON_KEYWORD_PARTITIONS_POSITION + '": {}\n'
              '    }}{}\n').format(p.label,
                                   p.offset,
                                   p.size,
                                   'true' if p.grow else 'false',
                                   p.guid,
                                   p.type_guid,
                                   p.flags,
                                   'true' if p.persist else 'false',
                                   'true' if p.ignore else 'false',
                                   'true' if p.ab else 'false',
                                   'true' if p.ab_expanded else 'false',
                                   p.position,
                                   '' if n == len(partitions) - 1 else ',')
    ret += ('  ]\n'
            '}\n')
    return ret

  def _lba_to_chs(self, lba):
    """Converts LBA to CHS.

    Arguments:
      lba: The sector number to convert.

    Returns:
      An array containing the CHS encoded the way it's expected in a
      MBR partition table.
    """
    # See https://en.wikipedia.org/wiki/Cylinder-head-sector
    num_heads = 255
    num_sectors = 63
    # If LBA isn't going to fit in CHS, return maximum CHS values.
    max_lba = 255*num_heads*num_sectors
    if lba > max_lba:
      return [255, 255, 255]
    c = lba / (num_heads*num_sectors)
    h = (lba / num_sectors) % num_heads
    s = lba % num_sectors
    return [h, (((c>>8) & 0x03)<<6) | (s & 0x3f), c & 0xff]

  def _generate_protective_mbr(self, settings):
    """Generate Protective MBR.

    Arguments:
      settings: A Settings object.

    Returns:
      A string with the binary protective MBR (512 bytes).
    """
    # See https://en.wikipedia.org/wiki/Master_boot_record for MBR layout.
    #
    # The first partition starts at offset 446 (0x1be).
    lba_start = 1
    lba_end = settings.disk_size/DISK_SECTOR_SIZE - 1
    start_chs = self._lba_to_chs(lba_start)
    end_chs = self._lba_to_chs(lba_end)
    pmbr = struct.pack('<446s'     # Bootloader code
                       'B'         # Status.
                       'BBB'       # CHS start.
                       'B'         # Partition type.
                       'BBB'       # CHS end.
                       'I'         # LBA of partition start.
                       'I'         # Number of sectors in partition.
                       '48x'       # Padding to get to offset 510 (0x1fe).
                       'BB',       # Boot signature.
                       '\xfa\xeb\xfe', # cli ; jmp $ (x86)
                       0x00,
                       start_chs[0], start_chs[1], start_chs[2],
                       0xee,       # MBR Partition Type: GPT protective MBR.
                       end_chs[0], end_chs[1], end_chs[2],
                       1,          # LBA start
                       lba_end,
                       0x55, 0xaa)
    return pmbr

  def _generate_gpt(self, partitions, settings, primary=True):
    """Generate GUID Partition Table.

    Arguments:
      partitions: A list of Partition objects.
      settings: A Settings object.
      primary: True to generate primary GPT, False to generate secondary.

    Returns:
      A string with the binary GUID Partition Table (33*512 bytes).
    """
    # See https://en.wikipedia.org/wiki/Master_boot_record for MBR layout.
    #
    # The first partition starts at offset 446 (0x1be).

    disk_num_lbas = settings.disk_size/DISK_SECTOR_SIZE
    if primary:
      current_lba = 1
      other_lba = disk_num_lbas - 1
      partitions_lba = 2
    else:
      current_lba = disk_num_lbas - 1
      other_lba = 1
      partitions_lba = disk_num_lbas - GPT_NUM_LBAS
    first_usable_lba = GPT_NUM_LBAS + 1
    last_usable_lba = disk_num_lbas - GPT_NUM_LBAS - 1

    part_array = []
    for p in partitions:
      part_array.append(struct.pack(
          '<16s'    # Partition type GUID.
          '16s'     # Partition instance GUID.
          'QQ'      # First and last LBA.
          'Q'       # Flags.
          '72s',    # Name (36 UTF-16LE code units).
          uuid.UUID(p.type_guid).get_bytes_le(),
          uuid.UUID(p.guid).get_bytes_le(),
          p.offset/DISK_SECTOR_SIZE,
          (p.offset + p.size)/DISK_SECTOR_SIZE - 1,
          p.flags,
          p.label.encode(encoding='utf-16le')))

    part_array.append(((128 - len(partitions))*128) * '\0')
    part_array_str = ''.join(part_array)

    partitions_crc32 = zlib.crc32(part_array_str) % (1<<32)

    header_crc32 = 0
    while True:
      header = struct.pack(
          '<8s'    # Signature.
          '4B'     # Version.
          'I'      # Header size.
          'I'      # CRC32 (must be zero during calculation).
          'I'      # Reserved (must be zero).
          'QQ'     # Current and Other LBA.
          'QQ'     # First and last usable LBA.
          '16s'    # Disk GUID.
          'Q'      # Starting LBA of array of partitions.
          'I'      # Number of partitions.
          'I'      # Partition entry size, in bytes.
          'I'      # CRC32 of partition array
          '420x',  # Padding to get to 512 bytes.
          'EFI PART',
          0x00, 0x00, 0x01, 0x00,
          92,
          header_crc32,
          0x00000000,
          current_lba, other_lba,
          first_usable_lba, last_usable_lba,
          uuid.UUID(settings.disk_guid).get_bytes_le(),
          partitions_lba,
          128,
          128,
          partitions_crc32)
      if header_crc32 != 0:
        break
      header_crc32 = zlib.crc32(header[0:92]) % (1<<32)

    if primary:
      return header + part_array_str
    else:
      return part_array_str + header

  def _generate_gpt_bin(self, partitions, settings):
    """Generate a bytearray representing partitions and settings.

    The blob will have three partition tables, laid out one after
    another: 1) Protective MBR (512 bytes); 2) Primary GPT (33*512
    bytes); and 3) Secondary GPT (33*512 bytes).

    The total size will be 34,304 bytes.

    Arguments:
      partitions: A list of Partition objects.
      settings: A Settings object.

    Returns:
      A bytearray() object.
    """
    protective_mbr = self._generate_protective_mbr(settings)
    primary_gpt = self._generate_gpt(partitions, settings)
    secondary_gpt = self._generate_gpt(partitions, settings, primary=False)
    ret = protective_mbr + primary_gpt + secondary_gpt
    return ret

  def _validate_disk_partitions(self, partitions, disk_size):
    """Check that a list of partitions have assigned offsets and fits on a
       disk of a given size.

    This function checks partition offsets and sizes to see if they may fit on
    a disk image.

    Arguments:
      partitions: A list of Partition objects.
      settings: Integer size of disk image.

    Raises:
      BptError: If checked condition is not satisfied.
    """
    for p in partitions:
      if not p.offset or p.offset < (GPT_NUM_LBAS + 1)*DISK_SECTOR_SIZE:
        raise BptError('Partition with label "{}" has no offset.'
                       .format(p.label))
      if not p.size or p.size < 0:
        raise BptError('Partition with label "{}" has no size.'
                        .format(p.label))
      if (p.offset + p.size) > (disk_size - GPT_NUM_LBAS*DISK_SECTOR_SIZE):
        raise BptError('Partition with label "{}" exceeds the disk '
                       'image size.'.format(p.label))

  def make_table(self,
                 inputs,
                 ab_suffixes=None,
                 partitions_offset_begin=None,
                 disk_size=None,
                 disk_alignment=None,
                 disk_guid=None,
                 guid_generator=None):
    """Implementation of the 'make_table' command.

    This function takes a list of input partition definition files,
    flattens them, expands A/B partitions, grows partitions, and lays
    out partitions according to alignment constraints.

    Arguments:
      inputs: List of JSON files to parse.
      ab_suffixes: List of the A/B suffixes (as a comma-separated string)
                   to use or None to not override.
      partitions_offset_begin: Size of disk partitions offset
                               begin or None to not override.
      disk_size: Size of disk or None to not override.
      disk_alignment: Disk alignment or None to not override.
      disk_guid: Disk GUID as a string or None to not override.
      guid_generator: A GuidGenerator or None to use the default.

    Returns:
      A tuple where the first argument is a JSON string for the resulting
      partitions and the second argument is the binary partition tables.

    Raises:
      BptParsingError: If an input file has an error.
      BptError: If another application-specific error occurs
    """
    partitions, settings = self._read_json(inputs)

    # Command-line arguments override anything specified in input
    # files.
    if disk_size:
      settings.disk_size = int(math.ceil(disk_size))
    if disk_alignment:
      settings.disk_alignment = int(disk_alignment)
    if partitions_offset_begin:
      settings.partitions_offset_begin = int(partitions_offset_begin)
    if ab_suffixes:
      settings.ab_suffixes = ab_suffixes.split(',')
    if disk_guid:
      settings.disk_guid = disk_guid

    if not guid_generator:
      guid_generator = GuidGenerator()

    # We need to know the disk size. Also round it down to ensure it's
    # a multiple of the sector size.
    if not settings.disk_size:
      raise BptError('Disk size not specified. Use --disk_size option '
                     'or specify it in an input file.\n')
    settings.disk_size = RoundToMultiple(settings.disk_size,
                                         DISK_SECTOR_SIZE,
                                         round_down=True)

    # Alignment must be divisible by disk sector size.
    if settings.disk_alignment % DISK_SECTOR_SIZE != 0:
      raise BptError(
          'Disk alignment size of {} is not divisible by {}.\n'.format(
              settings.disk_alignment, DISK_SECTOR_SIZE))

    if settings.partitions_offset_begin != 0:
      # Disk partitions offset begin size must be
      # divisible by disk sector size.
      if settings.partitions_offset_begin % settings.disk_alignment != 0:
        raise BptError(
            'Disk Partitions offset begin size of {} '
            'is not divisible by {}.\n'.format(
                settings.partitions_offset_begin, settings.disk_alignment))
      settings.partitions_offset_begin = max(settings.partitions_offset_begin,
                                           DISK_SECTOR_SIZE*(1 + GPT_NUM_LBAS))
      settings.partitions_offset_begin = RoundToMultiple(
          settings.partitions_offset_begin, settings.disk_alignment)

    # Expand A/B partitions and skip ignored partitions.
    expanded_partitions = []
    for p in partitions:
      if p.ignore:
        continue
      if p.ab and not p.ab_expanded:
        p.ab_expanded = True
        for suffix in settings.ab_suffixes:
          new_p = copy.deepcopy(p)
          new_p.label += suffix
          expanded_partitions.append(new_p)
      else:
        expanded_partitions.append(p)
    partitions = expanded_partitions

    # Expand Disk GUID if needed.
    if not settings.disk_guid or settings.disk_guid == JSON_KEYWORD_AUTO:
      settings.disk_guid = guid_generator.dispense_guid(0)

    # Sort according to 'position' attribute.
    partitions = sorted(partitions, cmp=lambda x, y: x.cmp(y))

    # Automatically generate GUIDs if the GUID is unset or set to
    # 'auto'. Also validate the rest of the fields.
    part_no = 1
    for p in partitions:
      p.expand_guid(guid_generator, part_no)
      p.validate()
      part_no += 1

    # Idenfify partition to grow and lay out partitions, ignoring the
    # one to grow. This way we can figure out how much space is left.
    #
    # Right now we only support a single 'grow' partition but we could
    # support more in the future by splitting up the available bytes
    # between them.
    grow_part = None
    # offset minimal size: DISK_SECTOR_SIZE*(1 + GPT_NUM_LBAS)
    offset = max(settings.partitions_offset_begin,
                 DISK_SECTOR_SIZE*(1 + GPT_NUM_LBAS))
    for p in partitions:
      if p.grow:
        if grow_part:
          raise BptError('Only a single partition can be automatically '
                         'grown.\n')
        grow_part = p
      else:
        # Ensure size is a multiple of DISK_SECTOR_SIZE by rounding up
        # (user may specify it as e.g. "1.5 GB" which is not divisible
        # by 512).
        p.size = RoundToMultiple(p.size, DISK_SECTOR_SIZE)
        # Align offset to disk alignment.
        offset = RoundToMultiple(offset, settings.disk_alignment)
        offset += p.size

    # After laying out (respecting alignment) all non-grow
    # partitions, check that the given disk size is big enough.
    if offset > settings.disk_size - DISK_SECTOR_SIZE*GPT_NUM_LBAS:
      raise BptError('Disk size of {} bytes is too small for partitions '
                     'totaling {} bytes.\n'.format(
                         settings.disk_size, offset))

    # If we have a grow partition, it'll starts at the next
    # available alignment offset and we can calculate its size as
    # follows.
    if grow_part:
      offset = RoundToMultiple(offset, settings.disk_alignment)
      grow_part.size = RoundToMultiple(
          settings.disk_size - DISK_SECTOR_SIZE*GPT_NUM_LBAS - offset,
          settings.disk_alignment,
          round_down=True)
      if grow_part.size < DISK_SECTOR_SIZE:
        raise BptError('Not enough space for partition "{}" to be '
                       'automatically grown.\n'.format(grow_part.label))

    # Now we can assign partition start offsets for all partitions,
    # including the grow partition.
    # offset minimal size: DISK_SECTOR_SIZE*(1 + GPT_NUM_LBAS)
    offset = max(settings.partitions_offset_begin,
                 DISK_SECTOR_SIZE*(1 + GPT_NUM_LBAS))
    for p in partitions:
      # Align offset.
      offset = RoundToMultiple(offset, settings.disk_alignment)
      p.offset = offset
      offset += p.size
    assert offset <= settings.disk_size - DISK_SECTOR_SIZE*GPT_NUM_LBAS

    json_str = self._generate_json(partitions, settings)

    gpt_bin = self._generate_gpt_bin(partitions, settings)

    return json_str, gpt_bin

  def make_disk_image(self, output, bpt, images, allow_empty_partitions=False):
    """Implementation of the 'make_disk_image' command.

    This function takes in a list of partitions images and a bpt file
    for the purpose of creating a raw disk image with a protective MBR,
    primary and secondary GPT, and content for each partition as specified.

    Arguments:
      output: Output file where disk image is to be written to.
      bpt: BPT JSON file to parse.
      images: List of partition image paths to be combined (as specified by
              bpt).  Each element is of the form.
              'PARTITION_NAME:/PATH/TO/PARTITION_IMAGE'
      allow_empty_partitions: If True, partitions defined in |bpt| need not to
                              be present in |images|. Otherwise an exception is
                              thrown if a partition is referenced in |bpt| but
                              not in |images|.

    Raises:
      BptParsingError: If an image file has an error.
      BptError: If another application-specific error occurs.
    """
    # Generate partition list and settings.
    partitions, settings = self._read_json([bpt], ab_collapse=False)

    # Validated partition sizes and offsets.
    self._validate_disk_partitions(partitions, settings.disk_size)

    # Sort according to 'offset' attribute.
    partitions = sorted(partitions, cmp=lambda x, y: cmp(x.offset, y.offset))

    # Create necessary tables.
    protective_mbr = self._generate_protective_mbr(settings)
    primary_gpt = self._generate_gpt(partitions, settings)
    secondary_gpt = self._generate_gpt(partitions, settings, primary=False)

    # Start at 0 offset for mbr and primary gpt.
    output.seek(0)
    output.write(protective_mbr)
    output.write(primary_gpt)

    # Create mapping of partition name to partition image file.
    image_file_names = {}
    try:
      for name_path in images:
        name, path = name_path.split(":")
        image_file_names[name] = path
    except ValueError as e:
      raise BptParsingError(name_path, 'Bad image argument {}.'.format(
                            images[i]))

    # Read image and insert in correct offset.
    for p in partitions:
      if p.label not in image_file_names:
        if allow_empty_partitions:
          continue
        else:
          raise BptParsingError(bpt.name, 'No content specified for partition'
                                ' with label {}'.format(p.label))

      input_image = ImageHandler(image_file_names[p.label])
      output.seek(p.offset)
      partition_blob = input_image.read(p.size)
      output.write(partition_blob)

    # Put secondary GPT and end of disk.
    output.seek(settings.disk_size - len(secondary_gpt))
    output.write(secondary_gpt)

  def query_partition(self, input_file, part_label, query_type, ab_collapse):
    """Implementation of the 'query_partition' command.

    This reads the partition definition file given by |input_file| and
    returns information of type |query_type| for the partition with
    label |part_label|.

    Arguments:
      input_file: A JSON file to parse.
      part_label: Label of partition to query information about.
      query_type: The information to query, see |QUERY_PARTITION_TYPES|.
      ab_collapse: If True, collapse A/B partitions.

    Returns:
      The requested information as a string or None if there is no
      partition with the given label.

    Raises:
      BptParsingError: If an input file has an error.
      BptError: If another application-specific error occurs
    """

    partitions, _ = self._read_json([input_file], ab_collapse)

    part = None
    for p in partitions:
      if p.label == part_label:
        part = p
        break

    if not part:
      return None

    value = part.__dict__.get(query_type)
    # Print out flags as a hex-value.
    if query_type == 'flags':
      return '{:#018x}'.format(value)
    return str(value)


class BptTool(object):
  """Object for bpttool command-line tool."""

  def __init__(self):
    """Initializer method."""
    self.bpt = Bpt()

  def run(self, argv):
    """Command-line processor.

    Arguments:
      argv: Pass sys.argv from main.
    """
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(title='subcommands')

    sub_parser = subparsers.add_parser(
        'version',
        help='Prints version of bpttool.')
    sub_parser.set_defaults(func=self.version)

    sub_parser = subparsers.add_parser(
        'make_table',
        help='Lays out partitions and creates partition table.')
    sub_parser.add_argument('--input',
                            help='Path to partition definition file.',
                            type=argparse.FileType('r'),
                            action='append')
    sub_parser.add_argument('--ab_suffixes',
                            help='Set or override A/B suffixes.')
    sub_parser.add_argument('--partitions_offset_begin',
                            help='Set or override disk partitions '
                                 'offset begin size.',
                            type=ParseSize)
    sub_parser.add_argument('--disk_size',
                            help='Set or override disk size.',
                            type=ParseSize)
    sub_parser.add_argument('--disk_alignment',
                            help='Set or override disk alignment.',
                            type=ParseSize)
    sub_parser.add_argument('--disk_guid',
                            help='Set or override disk GUID.',
                            type=ParseGuid)
    sub_parser.add_argument('--output_json',
                            help='JSON output file name.',
                            type=argparse.FileType('w'))
    sub_parser.add_argument('--output_gpt',
                            help='Output file name for MBR/GPT/GPT file.',
                            type=argparse.FileType('w'))
    sub_parser.set_defaults(func=self.make_table)

    sub_parser = subparsers.add_parser(
        'make_disk_image',
        help='Creates disk image for loaded with partitions.')
    sub_parser.add_argument('--output',
                            help='Path to image output.',
                            type=argparse.FileType('w'),
                            required=True)
    sub_parser.add_argument('--input',
                            help='Path to bpt file input.',
                            type=argparse.FileType('r'),
                            required=True)
    sub_parser.add_argument('--image',
                            help='Partition name and path to image file.',
                            metavar='PARTITION_NAME:PATH',
                            action='append')
    sub_parser.add_argument('--allow_empty_partitions',
                            help='Allow skipping partitions in bpt file.',
                            action='store_true')
    sub_parser.set_defaults(func=self.make_disk_image)

    sub_parser = subparsers.add_parser(
        'query_partition',
        help='Looks up informtion about a partition.')
    sub_parser.add_argument('--input',
                            help='Path to partition definition file.',
                            type=argparse.FileType('r'),
                            required=True)
    sub_parser.add_argument('--label',
                            help='Label of partition to look up.',
                            required=True)
    sub_parser.add_argument('--ab_collapse',
                            help='Collapse A/B partitions.',
                            action='store_true')
    sub_parser.add_argument('--type',
                            help='Type of information to look up.',
                            choices=QUERY_PARTITION_TYPES,
                            required=True)
    sub_parser.set_defaults(func=self.query_partition)

    args = parser.parse_args(argv[1:])
    args.func(args)

  def version(self, _):
    """Implements the 'version' sub-command."""
    print '{}.{}'.format(BPT_VERSION_MAJOR, BPT_VERSION_MINOR)

  def query_partition(self, args):
    """Implements the 'query_partition' sub-command."""
    try:
      result = self.bpt.query_partition(args.input,
                                        args.label,
                                        args.type,
                                        args.ab_collapse)
    except BptParsingError as e:
      sys.stderr.write('{}: Error parsing: {}\n'.format(e.filename, e.message))
      sys.exit(1)
    except BptError as e:
      sys.stderr.write('{}\n'.format(e.message))
      sys.exit(1)

    if not result:
      sys.stderr.write('No partition with label "{}".\n'.format(args.label))
      sys.exit(1)

    print result

  def make_table(self, args):
    """Implements the 'make_table' sub-command."""
    if not args.input:
      sys.stderr.write('Option --input is required one or more times.\n')
      sys.exit(1)

    try:
      (json_str, gpt_bin) = self.bpt.make_table(args.input, args.ab_suffixes,
                                                args.partitions_offset_begin,
                                                args.disk_size,
                                                args.disk_alignment,
                                                args.disk_guid)
    except BptParsingError as e:
      sys.stderr.write('{}: Error parsing: {}\n'.format(e.filename, e.message))
      sys.exit(1)
    except BptError as e:
      sys.stderr.write('{}\n'.format(e.message))
      sys.exit(1)

    if args.output_json:
      args.output_json.write(json_str)
    if args.output_gpt:
      args.output_gpt.write(gpt_bin)

  def make_disk_image(self, args):
    """Implements the 'make_disk_image' sub-command."""
    if not args.input:
      sys.stderr.write('Option --input is required.\n')
      sys.exit(1)
    if not args.output:
      sys.stderr.write('Option --ouptut is required.\n')
      sys.exit(1)

    try:
      self.bpt.make_disk_image(args.output,
                               args.input,
                               args.image,
                               args.allow_empty_partitions)
    except BptParsingError as e:
      sys.stderr.write('{}: Error parsing: {}\n'.format(e.filename, e.message))
      sys.exit(1)
    except 'BptError' as e:
      sys.stderr.write('{}\n'.format(e.message))
      sys.exit(1)

if __name__ == '__main__':
  tool = BptTool()
  tool.run(sys.argv)
