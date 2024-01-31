project('cvd', 'cpp', default_options: ['cpp_std=gnu++17'])

common_sources = [
  'android-base/file.cpp',
  'android-base/parsebool.cpp',
  'android-base/posix_strerror_r.cpp',
  'android-base/stringprintf.cpp',
  'android-base/strings.cpp',
  'android-base/threads.cpp',
  'android-base/logging.cpp',
  'cuttlefish/common/libs/fs/epoll.cpp',
  'cuttlefish/common/libs/fs/shared_buf.cc',
  'cuttlefish/common/libs/fs/shared_fd.cpp',
  'cuttlefish/common/libs/utils/archive.cpp',
  'cuttlefish/common/libs/utils/base64.cpp',
  'cuttlefish/common/libs/utils/environment.cpp',
  'cuttlefish/common/libs/utils/files.cpp',
  'cuttlefish/common/libs/utils/flag_parser.cpp',
  'cuttlefish/common/libs/utils/flags_validator.cpp',
  'cuttlefish/common/libs/utils/inotify.cpp',
  'cuttlefish/common/libs/utils/json.cpp',
  'cuttlefish/common/libs/utils/proc_file_utils.cpp',
  'cuttlefish/common/libs/utils/result.cpp',
  'cuttlefish/common/libs/utils/shared_fd_flag.cpp',
  'cuttlefish/common/libs/utils/subprocess.cpp',
  'cuttlefish/common/libs/utils/tee_logging.cpp',
  'cuttlefish/common/libs/utils/unix_sockets.cpp',
  'cuttlefish/common/libs/utils/users.cpp',
  'cuttlefish/host/libs/config/config_utils.cpp',
  'cuttlefish/host/libs/config/fetcher_config.cpp',
  'cuttlefish/host/libs/config/host_tools_version.cpp',
  'cuttlefish/host/libs/config/instance_nums.cpp',
  'cuttlefish/host/libs/web/build_api.cc',
  'cuttlefish/host/libs/web/build_string.cc',
  'cuttlefish/host/libs/web/credential_source.cc',
  'cuttlefish/host/libs/web/http_client/http_client.cc',
  'cuttlefish/host/libs/web/http_client/http_client_util.cc',
]

cvd_sources = [
  'cuttlefish/host/commands/cvd/acloud/config.cpp',
  'cuttlefish/host/commands/cvd/acloud/converter.cpp',
  'cuttlefish/host/commands/cvd/acloud/create_converter_parser.cpp',
  'cuttlefish/host/commands/cvd/client.cpp',
  'cuttlefish/host/commands/cvd/command_sequence.cpp',
  'cuttlefish/host/commands/cvd/common_utils.cpp',
  'cuttlefish/host/commands/cvd/driver_flags.cpp',
  'cuttlefish/host/commands/cvd/epoll_loop.cpp',
  'cuttlefish/host/commands/cvd/fetch/fetch_cvd.cc',
  'cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.cc',
  'cuttlefish/host/commands/cvd/flag.cpp',
  'cuttlefish/host/commands/cvd/frontline_parser.cpp',
  'cuttlefish/host/commands/cvd/handle_reset.cpp',
  'cuttlefish/host/commands/cvd/instance_lock.cpp',
  'cuttlefish/host/commands/cvd/instance_manager.cpp',
  'cuttlefish/host/commands/cvd/interruptible_terminal.cpp',
  'cuttlefish/host/commands/cvd/lock_file.cpp',
  'cuttlefish/host/commands/cvd/logger.cpp',
  'cuttlefish/host/commands/cvd/main.cc',
  'cuttlefish/host/commands/cvd/metrics/metrics_notice.cpp',
  'cuttlefish/host/commands/cvd/parser/cf_configs_common.cpp',
  'cuttlefish/host/commands/cvd/parser/cf_configs_instances.cpp',
  'cuttlefish/host/commands/cvd/parser/cf_flags_validator.cpp',
  'cuttlefish/host/commands/cvd/parser/cf_metrics_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/fetch_config_parser.cpp',
  'cuttlefish/host/commands/cvd/parser/instance/cf_boot_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/instance/cf_disk_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/instance/cf_graphics_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/instance/cf_security_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/instance/cf_streaming_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/instance/cf_vm_configs.cpp',
  'cuttlefish/host/commands/cvd/parser/launch_cvd_parser.cpp',
  'cuttlefish/host/commands/cvd/parser/launch_cvd_templates.cpp',
  'cuttlefish/host/commands/cvd/parser/load_configs_parser.cpp',
  'cuttlefish/host/commands/cvd/parser/selector_parser.cpp',
  'cuttlefish/host/commands/cvd/request_context.cpp',
  'cuttlefish/host/commands/cvd/reset_client_utils.cpp',
  'cuttlefish/host/commands/cvd/run_cvd_proc_collector.cpp',
  'cuttlefish/host/commands/cvd/run_server.cpp',
  'cuttlefish/host/commands/cvd/selector/arguments_lexer.cpp',
  'cuttlefish/host/commands/cvd/selector/arguments_separator.cpp',
  'cuttlefish/host/commands/cvd/selector/creation_analyzer.cpp',
  'cuttlefish/host/commands/cvd/selector/device_selector_utils.cpp',
  'cuttlefish/host/commands/cvd/selector/group_selector.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_database.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_database_impl.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_database_types.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_database_utils.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_group_record.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_record.cpp',
  'cuttlefish/host/commands/cvd/selector/instance_selector.cpp',
  'cuttlefish/host/commands/cvd/selector/selector_common_parser.cpp',
  'cuttlefish/host/commands/cvd/selector/selector_constants.cpp',
  'cuttlefish/host/commands/cvd/selector/start_selector_parser.cpp',
  'cuttlefish/host/commands/cvd/server.cc',
  'cuttlefish/host/commands/cvd/server_client.cpp',
  'cuttlefish/host/commands/cvd/server_command/acloud_command.cpp',
  'cuttlefish/host/commands/cvd/server_command/acloud_common.cpp',
  'cuttlefish/host/commands/cvd/server_command/acloud_mixsuperimage.cpp',
  'cuttlefish/host/commands/cvd/server_command/acloud_translator.cpp',
  'cuttlefish/host/commands/cvd/server_command/cmd_list.cpp',
  'cuttlefish/host/commands/cvd/server_command/display.cpp',
  'cuttlefish/host/commands/cvd/server_command/env.cpp',
  'cuttlefish/host/commands/cvd/server_command/fetch.cpp',
  'cuttlefish/host/commands/cvd/server_command/flags_collector.cpp',
  'cuttlefish/host/commands/cvd/server_command/fleet.cpp',
  'cuttlefish/host/commands/cvd/server_command/generic.cpp',
  'cuttlefish/host/commands/cvd/server_command/handler_proxy.cpp',
  'cuttlefish/host/commands/cvd/server_command/help.cpp',
  'cuttlefish/host/commands/cvd/server_command/host_tool_target.cpp',
  'cuttlefish/host/commands/cvd/server_command/host_tool_target_manager.cpp',
  'cuttlefish/host/commands/cvd/server_command/lint.cpp',
  'cuttlefish/host/commands/cvd/server_command/load_configs.cpp',
  'cuttlefish/host/commands/cvd/server_command/power.cpp',
  'cuttlefish/host/commands/cvd/server_command/reset.cpp',
  'cuttlefish/host/commands/cvd/server_command/restart.cpp',
  'cuttlefish/host/commands/cvd/server_command/serial_launch.cpp',
  'cuttlefish/host/commands/cvd/server_command/serial_preset.cpp',
  'cuttlefish/host/commands/cvd/server_command/shutdown.cpp',
  'cuttlefish/host/commands/cvd/server_command/snapshot.cpp',
  'cuttlefish/host/commands/cvd/server_command/start.cpp',
  'cuttlefish/host/commands/cvd/server_command/status.cpp',
  'cuttlefish/host/commands/cvd/server_command/status_fetcher.cpp',
  'cuttlefish/host/commands/cvd/server_command/subprocess_waiter.cpp',
  'cuttlefish/host/commands/cvd/server_command/try_acloud.cpp',
  'cuttlefish/host/commands/cvd/server_command/utils.cpp',
  'cuttlefish/host/commands/cvd/server_command/version.cpp',
  'cuttlefish/host/commands/cvd/server_constants.cpp',
  'cuttlefish/host/commands/cvd/types.cpp',
]

allocd_sources = [
  'allocd/alloc_utils.cpp',
  'allocd/allocd.cpp',
  'allocd/resource.cpp',
  'allocd/resource_manager.cpp',
  'allocd/utils.cpp',
]

cc = meson.get_compiler('cpp')

protoc = find_program('protoc', required : true)
deps = dependency('protobuf', required : true)
protoc_generator = generator(protoc,
  output    : ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
  arguments : ['--proto_path=@CURRENT_SOURCE_DIR@', '--cpp_out=@BUILD_DIR@', '@INPUT@'])

server = protoc_generator.process('cvd_server.proto')
server_dependency = declare_dependency(sources: server)

internal_config = protoc_generator.process('internal_config.proto')
internal_config_dependency = declare_dependency(sources: internal_config)

launch_cvd_proto = protoc_generator.process('launch_cvd.proto')
launch_cvd_proto_dependency = declare_dependency(sources: launch_cvd_proto)

user_config = protoc_generator.process('user_config.proto')
user_config_dependency = declare_dependency(sources: user_config)

client_analytics_proto = protoc_generator.process('clientanalytics.proto')
client_analytics_proto_dependency = declare_dependency(sources: client_analytics_proto)

internal_user_log_proto = protoc_generator.process('internal_user_log.proto')
internal_user_log_proto_dependency = declare_dependency(sources: internal_user_log_proto)

git_version_h = vcs_tag(
  command : ['git', 'describe'],
  fallback : 'unknown',
  input : 'build_version.h.in',
  output :'version.h',
)
git_version_h_dependency = declare_dependency(sources: git_version_h)

dependencies = [
  client_analytics_proto_dependency,
  dependency('fmt'),
  dependency('gflags'),
  dependency('jsoncpp'),
  dependency('libcurl'),
  dependency('libglog'),
  dependency('libxml-2.0'),
  dependency('openssl'),
  dependency('protobuf'),
  dependency('uuid'),
  dependency('zlib'),
  git_version_h_dependency,
  internal_config_dependency,
  internal_user_log_proto_dependency,
  launch_cvd_proto_dependency,
  server_dependency,
  user_config_dependency,
]

executable(
  'cvd',
  cpp_args: ['-Wno-reorder', '-Wno-unknown-pragmas', '-Wno-attributes', '-Wno-sign-compare', '-Wno-write-strings',  '-DNODISCARD_EXPECTED=true'],
  link_args: ['-pthread'],
  sources: common_sources + cvd_sources,
  dependencies: dependencies,
  include_directories: [
    '/usr/local/include',
    'cuttlefish',
  ],
)
executable(
  'allocd',
  cpp_args: ['-Wno-reorder', '-Wno-unknown-pragmas', '-Wno-attributes', '-Wno-sign-compare', '-Wno-write-strings', '-DNODISCARD_EXPECTED=true'],
  link_args: ['-pthread'],
  sources: common_sources + allocd_sources,
  dependencies: dependencies,
  include_directories: [
    '/usr/local/include',
    'cuttlefish',
  ],
)

