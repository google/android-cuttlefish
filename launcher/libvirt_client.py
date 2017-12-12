"""libvirt interface.

Primary purpose of this class is to aid communication between libvirt and other classes.
libvirt's preferred method of delivery of larger object is, sadly, xml (rather than objects).
"""

# pylint: disable=no-self-use

from xml.etree import ElementTree
import libvirt
import logging

class LibVirtClient(object):
    """Client of the libvirt library.
    """
    def __init__(self):
        # Open channel to QEmu instance running locally.
        self.lvch = libvirt.open('qemu:///system')
        if self.lvch is None:
            raise Exception('Could not open libvirt channel. Did you install libvirt package?')

        self.capabilities = None
        self._log = logging.getLogger()

        # Parse host capabilities. Confirm our CPU is capable of executing movbe instruction
        # which allows further compatibility with atom cpus.
        self.host_capabilities = ElementTree.fromstring(self.lvch.getCapabilities())
        self._log.info('Starting cuttlefish on %s', self.get_hostname())
        self._log.info('Max number of virtual CPUs: %d', self.get_max_vcpus())
        self._log.info('Supported virtualization type: %s', self.get_virtualization_type())


    def get_hostname(self):
        """Return name of the host that will run guest images.

        Returns:
            hostname as string.
        """
        return self.lvch.getHostname()


    def get_max_vcpus(self):
        """Query max number of VCPUs that can be used by virtual instance.

        Returns:
            number of VCPUs that can be used by virtual instance.
        """
        return self.lvch.getMaxVcpus(None)


    def get_virtualization_type(self):
        """Query virtualization type supported by host.

        Returns:
            string describing supported virtualization type.
        """
        return self.lvch.getType()


    def get_instance(self, name):
        """Get libvirt instance matching supplied name.

        Args:
            name Name of the virtual instance.
        Returns:
            libvirt instance or None, if no instance by that name was found.
        """
        return self.lvch.lookupByName(name)


    def create_instance(self, description):
        """Create new instance based on the XML description.

        Args:
            description XML string describing instance.
        Returns:
            libvirt domain representing started domain. Domain will be automatically
            destroyed when this handle is orphaned.
        """
        return self.lvch.createXML(description,
                                   libvirt.VIR_DOMAIN_START_AUTODESTROY)


    def get_cpu_features(self):
        """Get host capabilities from libvirt.

        Returns:
            set of CPU features reported by the host.
        """
        caps = self.capabilities or set()
        try:
            if self.capabilities is None:
                features = self.host_capabilities.findall('./host/cpu/feature')
                if features is None:
                    self._log.warning('no \'host.cpu.feature\' nodes reported by libvirt.')
                    return caps

                for feature in features:
                    caps.add(feature.get('name'))
            return caps

        finally:
            # Make sure to update self.capabilities with empty set if anything goes wrong.
            self.capabilities = caps


    def build_instance_name(self, instance_number):
        """Convert instance number to an instance id (or domain).

        Args:
            instance_number Number of Cuttlefish instance.
        Returns:
            string representing instance (domain) name.
        """
        return 'android_cuttlefish_{}'.format(instance_number)
