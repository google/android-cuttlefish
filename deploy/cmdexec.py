"""This file contains classes that facilitate command execution on local and
remote hosts.
"""

import logging
from subprocess import Popen, PIPE, STDOUT

LOG = logging.getLogger()
SSH_ARGS = [
    '-o', 'StrictHostKeyChecking=no',
    '-o', 'UserKnownHostsFile=/dev/null',
    '-q',
]


class _SshExecBackend(object):
    def __init__(self, server):
        self._server = server
        self._shell = Popen(['ssh', server] + SSH_ARGS,
                            stdin=PIPE, stdout=PIPE, stderr=STDOUT, close_fds=True)


    def shell(self):
        """Get shell process.

        Returns:
            Popen class interfacing shell.
        """
        return self._shell


    def copy(self, source, target):
        """Copy source file to target location.

        This command may throw if execution was unsuccessful.

        Args:
            source Source file location (either local or remote)
            target Target file location (either local or remote)

        Returns:
            None
        """
        LOG.info('Transferring file %s -> %s' % (source, target))
        proc = Popen(['scp'] + SSH_ARGS + [source, '%s:%s' % (self._server, target)],
                     stdin=PIPE, stdout=PIPE, close_fds=True)
        result = proc.wait()
        if result != 0:
            raise Exception('Could not complete file transfer: scp returned %d' % result)


class _LocalExecBackend(object):
    def __init__(self):
        self._shell = Popen(['/bin/sh'], stdin=PIPE, stdout=PIPE, stderr=STDOUT, close_fds=True)


    def shell(self):
        """Get shell process.

        Returns:
            Popen class interfacing shell.
        """
        return self._shell


    def copy(self, source, target):
        """Copy source file to target location.

        This command may throw if execution was unsuccessful.

        Args:
            source Source file location (must be local)
            target Target file location (must be local)

        Returns:
            None
        """
        LOG.info('Copying file %s -> %s' % (source, target))
        proc = Popen(['cp', source, target], stdin=PIPE, stdout=PIPE, close_fds=True)
        result = proc.wait()
        if result != 0:
            raise Exception('Could not complete file transfer: scp returned %d' % result)


class Target(object):
    """RemoteServer allows faster command execution on remote server by keeping a
    single channel open at all times.
    """
    def __init__(self, backend):
        self.backend = backend


    @staticmethod
    def for_localhost():
        """Create new command executor for localhost.

        Commands will be invoked via /bin/sh.

        Returns:
            New instance of CmdExec.
        """
        return Target(_LocalExecBackend())


    @staticmethod
    def for_remote_host(server):
        """Create new command executor for remote server.

        Commands will be invoked via SSH.

        Args:
            server SSH server name (either IP address or server configured via ssh_config)

        Returns:
            New instance of CmdExec.
        """
        return Target(_SshExecBackend(server))


    def execute(self, command):
        """Execute supplied command on remote server.

        Args:
            command Command or expression to be executed on remote server.

        Returns:
            command output.
        """
        result = self.backend.shell().poll()
        if result:
            raise Exception('Server connection died:', result)

        header = '-=-=-=- COMMAND HEADER -=-=-=-'
        footer = '-=-=-=- COMMAND FOOTER -=-=-=-'

        LOG.info('Executing: %s', command)
        self.backend.shell().stdin.write(
            '\necho \'%s\'; ( %s ) || true; echo \'%s\'\n' % (header, command, footer))
        out = []
        found_header = False

        while True:
            line = self.backend.shell().stdout.readline().strip()
            # Search for header
            if not found_header:
                if line == header:
                    found_header = True
                continue

            if line == footer:
                break

            out.append(line)

        return out


    def copy(self, source, target):
        """Copy source file to target location.

        This call may throw if command execution was not successful.

        Args:
            source Source file name
            target Target file name
        """
        self.backend.copy(source, target)
