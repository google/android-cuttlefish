"""Fetch and Deploy Cuttlefish images to specified GCE instance.
"""

import argparse
import logging
import sys
from deploy import DeployAction
from config import ConfigAction

LOG_FORMAT = "%(levelname)1.1s %(asctime)s %(process)d %(filename)s:%(lineno)d] %(message)s"
LOG = logging.getLogger()


def setup_logger():
    """Set up logging mechanism.

    Logging mechanism will print logs out to standard error.
    """
    stdout_handler = logging.StreamHandler(sys.stderr)
    logging.basicConfig(
        level=logging.DEBUG,
        format=LOG_FORMAT,
        handlers=[stdout_handler])


def main():
    """Main entry point for the deploy script."""
    setup_logger()
    action = None

    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='action')
    DeployAction.configure(subparsers.add_parser('deploy'))
    ConfigAction.configure(subparsers.add_parser('config'))

    args = parser.parse_args()

    if args.action == 'deploy':
        action = DeployAction()
    elif args.action == 'config':
        action = ConfigAction()
    else:
        print('No action specified.')
        sys.exit(1)

    try:
        action.execute(args)
    except Exception as exception:
        LOG.exception('Could not complete operation: %s', exception)


if __name__ == '__main__':
    main()
