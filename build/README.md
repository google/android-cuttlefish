## Custom Actions

To add custom actions to the WebRTC control panel, create a custom action config
JSON file in your virtual device product makefile directory, create a
`prebuilt_etc_host` module for the JSON file with `sub_dir`
`cvd_custom_action_config`, then set the build variable
`SOONG_CONFIG_cvd_custom_action_config` to the name of that module. For example:

```
Android.bp:
  prebuilt_etc_host {
      name: "my_custom_action_config.json",
      src: "my_custom_action_config.json",
      // The sub_dir must always equal the following value:
      sub_dir: "cvd_custom_action_config",
  }

my_virtual_device.mk:
  SOONG_CONFIG_NAMESPACES += cvd
  SOONG_CONFIG_cvd += custom_action_config
  SOONG_CONFIG_cvd_custom_action_config := my_custom_action_config.json
```

TODO(b/171709037): Add documentation to source.android.com

See https://source.android.com/setup/create/cuttlefish-control-panel for
detailed information about the format of the config file.
