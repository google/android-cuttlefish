"""Initialize and manage file partition images.
"""

import logging
import os
import tempfile

class FilePartition(object):
    """FilePartition class manages access to partition image files.
    """
    def __init__(self, fileobj):
        self.file = fileobj
        self._log = logging.getLogger()


    def name(self):
        """Return name of the partition image file."""
        return self.file.name


    def _initialize_filesystem(self):
        self._log.info('Initializing %s', self.file.name)
        cmd = os.popen("/sbin/mkfs.ext4 -F %s" % (self.file.name))
        out = [l for l in cmd.xreadlines()]
        if cmd.close() is not None:
            raise Exception('Could not initialize filesystem:\n\t%s' %
                            ('\n\t'.join(out)))
        self._log.info('Initialized %s', self.file.name)


    @staticmethod
    def from_existing_file(filename):
        """Open an existing partition file.

        This method will throw an IOError if a specified file
        does not exist.

        Args:
            filename Name of the existing file to open.
        Returns:
            new FilePartition instance.
        """
        return FilePartition(open(filename, 'r+'))


    @staticmethod
    def create(filename, size_mb):
        """Create and initialize a specific file of a specified size.

        Create or update existing file; truncate it to specified size and
        initialize it with ext4 filesystem.

        This method will throw an IOError if a file could not be created with
        the requested name.

        Args:
            filename Name of the existing file to open.
            size_mb Requested size (in megabytes) of the new partition file.
        Returns:
            new FilePartition instance.
        """
        tmpfile = open(filename, 'w+')
        tmpfile.truncate(size_mb << 20)
        out = FilePartition(tmpfile)
        out._initialize_filesystem()
        return out


    @staticmethod
    def create_temp(filename, size_mb):
        """Create and initialize a unique temporary file of a specified size.

        Create a new temporary file named /tmp/[filename]-XXXXXX.img and
        initialize it with ext4 filesystem.
        File created using this method will be deleted once it is no longer
        used.

        This method will throw an IOError if a file could not be created.

        Args:
            filename Name of the existing file to open.
            size_mb Requested size (in megabytes) of the new partition file.
        Returns:
            new FilePartition instance.
        """
        tmpfile = tempfile.NamedTemporaryFile(prefix=filename + '-', suffix='.img')
        tmpfile.truncate(size_mb << 20)
        out = FilePartition(tmpfile)
        out._initialize_filesystem()
        return out
