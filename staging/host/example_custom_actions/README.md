To try out the custom action config and action server in this path, set the
following build vars:

```
$(call soong_config_set, cvd, custom_action_config, cuttlefish_example_action_config.json)
$(call soong_config_append, cvd, custom_action_servers, cuttlefish_example_action_server)
```

See `device/google/cuttlefish/build/README.md` for more information.
