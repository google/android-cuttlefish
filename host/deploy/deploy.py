"""Fetch and Deploy Cuttlefish images to specified GCE instance.
"""

import os
from cmdexec import Target

class DeployAction(object):
    """This class manages deploying cuttlefish image to remote server.
    """
    def __init__(self):
        pass


    @staticmethod
    def configure(parser):
        """Set up command line argument parser."""
        parser.add_argument('--system_build', type=str, required=False,
                            default='latest',
                            help='Build number to fetch from Android Build server.')
        parser.add_argument('--system_branch', type=str, required=False,
                            default='git_oc-gce-dev',
                            help='Android Build branch providing system images.')
        parser.add_argument('--system_target', type=str, required=False,
                            default='cf_x86_phone-userdebug',
                            help='Android Build target providing system images.')
        parser.add_argument('--kernel_build', type=str, required=False,
                            default='latest',
                            help='Build number to fetch from Android Build server.')
        parser.add_argument('--kernel_branch', type=str, required=False,
                            default='kernel-n-dev-android-gce-3.18-x86_64',
                            help='Android Build branch providing kernel images.')
        parser.add_argument('--kernel_target', type=str, required=False,
                            default='kernel',
                            help='Android Build target providing kernel images.')
        parser.add_argument('-i', '--instance', type=str, required=True,
                            help='IP address of GCE instance.')
        parser.add_argument('--tmpdir', type=str, required=False,
                            default='/tmp',
                            help='Temporary folder location.')
        parser.add_argument('--keep', action='store_true',
                            help='Keep downloaded archive file after completion '
                            '(=do not clean up).')
        parser.add_argument('--instance_folder', type=str, required=False,
                            default='/srv/cf',
                            help='Folder on the remote machine where images should be deployed.')
        parser.add_argument('--force_update', type=str, required=False,
                            help='Comma separated list of artifacts to force update.')


    def _to_build_id(self, selector):
        if selector == 'latest':
            return '--latest'
        return '--bid='+selector


    def execute(self, args):
        """Deploy Cuttlefish image to GCE."""
        # Do all work in temp folder.
        # Allow system to fail here with an exception.
        os.chdir(args.tmpdir)
        target_dir = '%s/%s' % (args.instance_folder, args.system_build)
        temp_image = '%s-%s.img' % (args.system_target, args.system_build)

        try:
            loc_tgt = Target.for_localhost()
            ssh_tgt = Target.for_remote_host(args.instance)

            system_build = self._to_build_id(args.system_build)
            kernel_build = self._to_build_id(args.kernel_build)
            force_update = args.force_update.split(',')

            if not os.path.exists('system.img'):
                if not os.path.exists(temp_image):
                    loc_tgt.execute(
                        '/google/data/ro/projects/android/fetch_artifact '
                        '%s --branch=%s --target=%s '
                        '\'cf_x86_phone-img-*\' \'%s\'' %
                        (system_build, args.system_branch, args.system_target, temp_image))
                loc_tgt.execute('unzip -u \'%s\' system.img' % temp_image)

            if not os.path.exists('ramdisk.img'):
                loc_tgt.execute(
                    '/google/data/ro/projects/android/fetch_artifact '
                    '%s --branch=%s --target=%s ramdisk.img' %
                    (system_build, args.system_branch, args.system_target))

            if not os.path.exists('kernel'):
                loc_tgt.execute(
                    '/google/data/ro/projects/android/fetch_artifact '
                    '%s --branch=%s --target=%s '
                    'bzImage kernel' %
                    (kernel_build, args.kernel_branch, args.kernel_target))

            # Give user and libvirt access rights to specified folder.
            # Remote directory appears as 'no access rights' except for included
            # users.
            ssh_tgt.execute('sudo mkdir -p %s' % target_dir)
            ssh_tgt.execute('sudo chmod ugo= %s' % target_dir)
            ssh_tgt.execute('sudo setfacl -m g:libvirt:rwx %s' % target_dir)
            ssh_tgt.execute('sudo setfacl -m u:libvirt-qemu:rwx %s' % target_dir)

            # Copy files to remote server location.
            upload_list = [
                'kernel',
                'ramdisk.img',
                'system.img'
            ]
            for file_name in upload_list:
                update = file_name in force_update

                if not update:
                    out = ssh_tgt.execute('test -f %s/%s; echo $?' % (target_dir, file_name))
                    update = out[0] == '1'

                if update:
                    ssh_tgt.copy(file_name, target_dir)

            ssh_tgt.execute('cp /usr/share/cuttlefish-common/gce_ramdisk.img %s' % target_dir)
            ssh_tgt.execute('setfacl -m g:libvirt:rw %s/*' % target_dir)
            ssh_tgt.execute('setfacl -m u:libvirt-qemu:rw %s/*' % target_dir)

        finally:
            if not args.keep:
                os.unlink(temp_image)
                os.unlink('boot.img')
                os.unlink('kernel')
                os.unlink('system.img')
