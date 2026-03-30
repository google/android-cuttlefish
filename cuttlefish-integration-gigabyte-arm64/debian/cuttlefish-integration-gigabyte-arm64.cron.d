#
# Regular cron jobs for the cuttlefish-integration-gigabyte-arm64 package.
#
# Workarounds for Network-Manager
@reboot root sleep 100 && /usr/bin/gig_amp_fix_nm_config
