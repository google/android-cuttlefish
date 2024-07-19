#
# Regular cron jobs for the metapackage-google package.
#
# Workarounds for Network-Manager
@reboot root sleep 100 && /usr/sbin/dhclient
@reboot root sleep 150 && su vsoc-01 -c '/bin/bash /usr/share/metapackage-google/cf_docker_co_run.sh'
