"""Guest definition.
"""
# pylint: disable=too-many-instance-attributes,no-self-use

import logging
from xml.etree import ElementTree as ET

import libvirt

class GuestDefinition(object):
    """Guest resource requirements definition.

    Args:
        lvc LibVirtClient instance.
    """
    def __init__(self, lvc):
        self._log = logging.getLogger()
        self._cmdline = None
        self._initrd = None
        self._instance_name = None
        self._instance_id = None
        self._kernel = None
        self._memory_mb = None
        self._net_mobile_bridge = None
        self._iv_vectors = None
        self._iv_socket_path = None
        self._part_cache = None
        self._part_data = None
        self._part_ramdisk = None
        self._part_system = None
        self._vcpus = None
        self._vmm_path = None
        # Initialize the following fields. These should not be configured by user.
        self._num_net_interfaces = 0
        self._num_ttys_interfaces = 0
        self._num_virtio_channels = 0

        self._lvc = lvc
        self.set_instance_id(1)
        self.set_num_vcpus(1)
        self.set_memory_mb(512)
        self.set_kernel('vmlinuz')
        self.set_initrd('initrd.img')
        self.set_net_mobile_bridge(None)


    def set_instance_id(self, inst_id):
        """Set instance ID of this guest.

        Args:
            inst_id Numerical instance ID, starting at 1.
        """
        if inst_id < 1 or inst_id > 255:
            self._log.error('Ignoring invalid instance id requested: %d.', inst_id)
            return
        self._instance_id = inst_id
        self._instance_name = None


    def get_instance_name(self):
        """Return name of this instance.
        """
        if self._instance_name is None:
            self._instance_name = self._lvc.build_instance_name(self._instance_id)
        return self._instance_name


    def set_num_vcpus(self, cpus):
        """Set number of virtual CPUs for this guest.

        Number of VCPUs will be checked for sanity and local maximums.
        """
        max_cpus = self._lvc.get_max_vcpus()
        if cpus < 0 or cpus > max_cpus:
            self._log.error('Ignoring invalid number of vcpus requested (%d); ' +
                            'max is %d.', cpus, max_cpus)
            return
        self._vcpus = cpus


    def set_memory_mb(self, memory_mb):
        """Set memory size allocated for the quest in MB.

        Args:
            memory_mb Total amount of memory allocated for the guest in MB.
        """
        if memory_mb < 0:
            self._log.error('Ignoring invalid amount of memory requested (%d).', memory_mb)
            return
        self._memory_mb = memory_mb


    def set_kernel(self, kernel):
        """Specify kernel path.

        Args:
            kernel FilePartition object pointing to vmlinuz file.
        """
        self._kernel = kernel


    def set_initrd(self, initrd):
        """Specify initrd path.

        Args:
            initrd FilePartition object pointing to initrd.img file.
        """
        self._initrd = initrd


    def set_cmdline(self, cmdline):
        """Specify kernel command line arguments.

        Args:
            cmdline Additional kernel command line arguments.
        """
        self._cmdline = cmdline


    def set_cf_ramdisk(self, ramdisk):
        """Specify cuttlefish ramdisk path.

        Args:
            ramdisk Cuttlefish built 'ramdisk.img' FilePartition object.
        """
        self._part_ramdisk = ramdisk


    def set_cf_system_partition(self, system):
        """Specify cuttlefish system partition.

        Args:
            system Cuttlefish built 'system.img' FilePartition object.
                   This partition will be mounted under /system on
                   Android device.
        """
        self._part_system = system


    def set_cf_data_partition(self, data):
        """Specify cuttlefish data partition.

        Args:
            data Data FilePartition object. This partition will be
                 mounted under /data on Android device.
        """
        self._part_data = data


    def set_cf_cache_partition(self, cache):
        """Specify cuttlefish cache partition.

        Args:
            cache Cache FilePartition object. This partition will be
                  mounted under /cache on Android device.
        """
        self._part_cache = cache


    def set_net_mobile_bridge(self, bridge):
        """Specify mobile network bridge name.

        Args:
            bridge Name of the mobile network bridge.
        """
        if bridge is not None:
            # TODO(ender): check if bridge exists.
            pass
        self._net_mobile_bridge = bridge


    def set_vmm_path(self, path):
        """Specify path to virtual machine monitor that will be running our guest.

        Args:
            path Path to virtual machine monitor, eg. /usr/bin/qemu.
        """
        self._vmm_path = path


    def set_ivshmem_vectors(self, num):
        """Specify number of IV Shared Memory vectors.

        Args:
            num Number of vectors (non-negative).
        """
        if num < 0:
            self._log.error('Invalid number of iv shared memory vectors: %d', num)
            return
        self._iv_vectors = num


    def set_ivshmem_socket_path(self, path):
        """Specify path to unix socket managed by IV Shared Memory daemon.

        Args:
            path Path to unix domain socket.
        """
        self._iv_socket_path = path


    def _configure_vm(self, tree):
        """Create basic guest details.

        Args:
            tree Top level 'domain' element of the XML tree.
        """
        ET.SubElement(tree, 'name').text = self.get_instance_name()
        ET.SubElement(tree, 'on_poweroff').text = 'destroy'
        ET.SubElement(tree, 'on_reboot').text = 'restart'
        # TODO(ender): should this be restart?
        ET.SubElement(tree, 'on_crash').text = 'destroy'
        ET.SubElement(tree, 'vcpu').text = str(self._vcpus)
        ET.SubElement(tree, 'memory').text = str(self._memory_mb << 10)


    def _configure_kernel(self, tree):
        """Configure boot parameters for guest.

        Args:
            tree Top level 'domain' element of the XML tree.
        """
        node = ET.SubElement(tree, 'os')
        desc = ET.SubElement(node, 'type')
        desc.set('arch', 'x86_64')
        desc.set('machine', 'pc')
        desc.text = 'hvm'

        ET.SubElement(node, 'kernel').text = self._kernel.name()
        ET.SubElement(node, 'initrd').text = self._initrd.name()
        if self._cmdline is not None:
            ET.SubElement(node, 'cmdline').text = self._cmdline


    def _append_source(self, elem, stype, spath):
        """Append source type to specified element.

        Args:
          elem Target element that will receive new source,
          stype Source type (currently supported: file, unix),
          spath Source path.
        """
        elem.set('type', stype)
        src = ET.SubElement(elem, 'source')
        if stype == 'file':
            src.set('path', spath)
        elif stype == 'unix':
            src.set('mode', 'bind')
            src.set('path', spath)


    def _build_device_serial_port(self, interactive):
        """Configure serial ports for guest.

        More useful information can be found here:
        https://libvirt.org/formatdomain.html#elementCharSerial

        Args:
          interactive True, if serial port should be served as interactive
                      over Unix Domain Socket.
        """
        index = self._num_ttys_interfaces
        self._num_ttys_interfaces += 1
        path = '/tmp/%s-ttyS%d.log' % (self.get_instance_name(), index)
        tty = ET.Element('serial')
        if interactive:
            self._append_source(tty, 'unix', path)
            self._log.info('Interactive serial port set up. To access the interactive console run:')
            self._log.info('$ sudo socat file:$(tty),raw,echo=0 %s' % path)
        else:
            self._append_source(tty, 'file', path)
        ET.SubElement(tty, 'target').set('port', str(index))
        self._log.info('Serial port %d will send data to %s', index, path)
        return tty


    def _build_device_virtio_channel(self, purpose, stype):
        """Build fast paravirtualized virtio channel.

        More useful information can be found here:
        https://libvirt.org/formatdomain.html#elementCharSerial

        Args:
          purpose Human understandable purpose of this channel ('console', 'logcat', ...)
          stype Type of this channel (see _append_source).
        """
        self._num_virtio_channels += 1
        index = self._num_virtio_channels
        path = '/tmp/%s-%d-%s.log' % (self.get_instance_name(), index, purpose)
        vio = ET.Element('channel')
        self._append_source(vio, stype, path)
        tgt = ET.SubElement(vio, 'target')
        tgt.set('type', 'virtio')
        tgt.set('name', 'vport0p%d' % index)
        adr = ET.SubElement(vio, 'address')
        adr.set('type', 'virtio-serial')
        adr.set('controller', '0')
        adr.set('bus', '0')
        adr.set('port', str(index))
        self._log.info('Virtio channel %d will send data to %s', index, path)
        return vio


    def _build_device_disk_node(self, path, name, target_dev):
        """Create disk node for guest.

        More useful information can be found here:
        https://libvirt.org/formatdomain.html#elementsDisks

        Args:
            path Path to file containing partition or disk image.
            name Purpose of partition or disk image.
            target_dev Target device.
        """
        bus = 'ide'
        if target_dev.startswith('sd'):
            bus = 'sata'
        elif target_dev.startswith('vd'):
            bus = 'virtio'

        if path is None:
            raise Exception('No file specified for %s; (%s) %s is not available.',
                            name, bus, target_dev)

        disk = ET.Element('disk')
        disk.set('type', 'file')
        # disk.set('snapshot', 'external')
        drvr = ET.SubElement(disk, 'driver')
        drvr.set('name', 'qemu')
        drvr.set('type', 'raw')
        drvr.set('io', 'threads')
        trgt = ET.SubElement(disk, 'target')
        trgt.set('dev', target_dev)
        trgt.set('bus', bus)
        srce = ET.SubElement(disk, 'source')
        srce.set('file', path)
        return disk


    def _build_mac_address(self, index):
        """Create mac address from local instance number.
        """
        return '00:41:56:44:%02X:%02X' % (self._instance_id, index + 1)


    def _build_device_net_node(self, local_node, bridge):
        """Create virtual ethernet for guest.

        More useful information can be found here:
        https://libvirt.org/formatdomain.html#elementsNICSVirtual
        https://wiki.libvirt.org/page/Virtio

        Args:
            local_node Name of the local interface.
            bridge Name of the corresponding bridge.
        """
        index = self._num_net_interfaces
        self._num_net_interfaces += 1
        net = ET.Element('interface')
        net.set('type', 'bridge')
        ET.SubElement(net, 'source').set('bridge', bridge)
        ET.SubElement(net, 'mac').set('address', self._build_mac_address(index))
        ET.SubElement(net, 'model').set('type', 'e1000')
        ET.SubElement(net, 'target').set('dev', '%s%d' % (local_node, self._instance_id))
        return net


    def _configure_devices(self, tree):
        """Configure guest devices.

        Args:
            tree Top level 'domain' element of the XML tree.
        """
        dev = ET.SubElement(tree, 'devices')
        if self._vmm_path:
            ET.SubElement(dev, 'emulator').text = self._vmm_path
        dev.append(self._build_device_serial_port(True))
        dev.append(self._build_device_virtio_channel('logcat', 'file'))
        dev.append(self._build_device_virtio_channel('usb', 'unix'))
        dev.append(self._build_device_disk_node(self._part_ramdisk.name(), 'ramdisk', 'vda'))
        dev.append(self._build_device_disk_node(self._part_system.name(), 'system', 'vdb'))
        dev.append(self._build_device_disk_node(self._part_data.name(), 'data', 'vdc'))
        dev.append(self._build_device_disk_node(self._part_cache.name(), 'cache', 'vdd'))
        dev.append(self._build_device_net_node('amobile', self._net_mobile_bridge))
        dev.append(self._configure_rng())


    def _configure_features(self, features, tree):
        """Configure top-level features.

        Args:
            features List of strings of features to turn on.
            tree Top level 'domain' element of the XML tree.
        """
        node = ET.SubElement(tree, 'features')
        for feature in features:
          ET.SubElement(node, feature)


    def _configure_ivshmem(self, tree):
        """Configure InterVM Shared Memory region.

        Args:
            tree Top level 'domain' element of the XML tree.
        """
        if self._iv_vectors:
            cmd = ET.SubElement(tree, 'qemu:commandline')
            ET.SubElement(cmd, 'qemu:arg').set('value', '-chardev')
            ET.SubElement(cmd, 'qemu:arg').set(
                'value', 'socket,path=%s,id=ivsocket' % (self._iv_socket_path))
            ET.SubElement(cmd, 'qemu:arg').set('value', '-device')
            ET.SubElement(cmd, 'qemu:arg').set(
                'value', 'ivshmem-doorbell,chardev=ivsocket,vectors=%d' % (self._iv_vectors)
            )


    def _configure_rng(self):
        """Returns a node with the device definition for the rng.

        """
        rng = ET.Element('rng')
        rng.set('model', 'virtio')
        rate = ET.SubElement(rng, 'rate')
        rate.set('period', '2000')
        rate.set('bytes', '1234')
        backend = ET.SubElement(rng, 'backend')
        backend.set('model', 'random')

        # Libvirt pre 1.3.4 could not use urandom as entropy source.
        if libvirt.getVersion() > 1003003:
            backend.text = '/dev/urandom'
        else:
            self._log.warning('Your libvirt version cannot use /dev/urandom as entropy source.')
            self._log.warning('Unless your /dev/random can supply enough entropy, cuttlefish may '
                              'not work properly.')
            backend.text = '/dev/random'
        return rng


    def to_xml(self):
        """Build XML document describing guest properties.

        The created document will be used directly by libvirt to create corresponding
        virtual machine.

        Returns:
          string containing an XML document describing a VM.
        """
        tree = ET.Element('domain')
        tree.set('type', 'kvm')
        tree.set('xmlns:qemu', 'http://libvirt.org/schemas/domain/qemu/1.0')

        self._configure_vm(tree)
        self._configure_features([
            'acpi',
            'apic',
            'hap'], tree)
        self._configure_kernel(tree)
        self._configure_devices(tree)
        self._configure_ivshmem(tree)

        return ET.tostring(tree).decode('utf-8')
