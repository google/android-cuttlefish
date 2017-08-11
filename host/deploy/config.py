"""Fetch and Deploy Cuttlefish images to specified GCE instance.
"""

from cmdexec import Target

class ConfigAction(object):
    """This class configures remote instance.
    """
    def __init__(self):
        pass


    @staticmethod
    def configure(parser):
        """Set up command line argument parser."""
        parser.add_argument('-i', '--instance', type=str, required=True,
                            help='IP address of GCE instance.')
        parser.add_argument('--instance_folder', type=str, required=False,
                            default='/srv/cf',
                            help='Folder on the remote machine where images should be deployed.')


    def execute(self, args):
        """Configure remote instance."""
        try:
            ssh_tgt = Target.for_remote_host(args.instance)
            # Configure current user to allow them to use libvirt.
            ssh_tgt.execute('sudo usermod -a -G libvirt ${USER}')
            ssh_tgt.execute('newgrp libvirt')

            # Give user and libvirt access rights to specified folder.
            # Remote directory appears as 'no access rights' except for included
            # users.
            ssh_tgt.execute('sudo mkdir -p %s' % args.instance_folder)
            ssh_tgt.execute('sudo chmod ugo= %s' % args.instance_folder)
            ssh_tgt.execute('sudo setfacl -m g:libvirt:rwx %s' % args.instance_folder)
            ssh_tgt.execute('sudo setfacl -m u:libvirt-qemu:rwx %s' % args.instance_folder)

            # Configure libvirt to allow qemu to connect to our sockets.
            ssh_tgt.execute('sudo sed -i\'\' \''
                            's/[#\\s]*security_driver = ".*"\\s*$/security_driver = "none"/g'
                            '\' /etc/libvirt/qemu.conf')
            ssh_tgt.execute('sudo service libvirtd restart')
            ssh_tgt.execute('virsh net-create /usr/share/cuttlefish-common/network-abr0.xml')

        finally:
            pass
