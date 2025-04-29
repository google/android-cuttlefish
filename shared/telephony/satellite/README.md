# Geofence Data Description
- The file `sats2.dat` contains the geofence data for the intersection of the territories of US, CA,
PR, AU, EU and Skylo satellite coverage.
- Please refer [here](https://googleplex-android-review.git.corp.google.com/c/device/google/cuttlefish/+/33018164/3/shared/overlays/core/res/values/config.xml#32:~:text=config_oem_enabled_satellite_country_codes) for more detail country information.
- This data has better fidelity than previous versions.

- The file `satellite_access_config.json` contains satellite information(name, position,
band, earfcn, tagId) for each region.