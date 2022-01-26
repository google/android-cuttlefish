## Custom Actions

To add custom actions to the WebRTC control panel:

*   Create a custom action config JSON file in your virtual device product
    makefile directory.
*   Create a `prebuilt_etc_host` module for the JSON file with `sub_dir`
    `cvd_custom_action_config`
*   Set the Soong config variable `custom_action_config` in the `cvd` namespace
    to the name of that module. For example:

    ```
    Android.bp:
      prebuilt_etc_host {
          name: "my_custom_action_config.json",
          src: "my_custom_action_config.json",
          // The sub_dir must always equal the following value:
          sub_dir: "cvd_custom_action_config",
      }

    my_virtual_device.mk:
      $(call soong_config_set, cvd, custom_action_config, my_custom_action_config.json)
    ```

TODO(b/171709037): Add documentation to source.android.com

See https://source.android.com/setup/create/cuttlefish-control-panel for
detailed information about the format of the config file.
