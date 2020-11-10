To try out the custom action config and action server in this path, set the
following build vars:

```
SOONG_CONFIG_NAMESPACES += cvd
SOONG_CONFIG_cvd += custom_action_config custom_action_servers

SOONG_CONFIG_cvd_custom_action_config := cuttlefish_example_action_config.json
SOONG_CONFIG_cvd_custom_action_servers += cuttlefish_example_action_server
```

See `device/google/cuttlefish/build/README.md` for more information.
