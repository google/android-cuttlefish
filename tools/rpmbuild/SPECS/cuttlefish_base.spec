Name:           cuttlefish-base
Version:        1.4.0
Release:        1%{?dist}
Summary:        Cuttlefish Android Virtual Device

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish

BuildArch:      x86_64 aarch64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# Note: `ncurses-compat-libs` require EPEL repository and `protobuf` requires CRB repository.
BuildRequires:  go, cmake, gcc-c++, ncurses-compat-libs, protobuf-devel, protobuf-compiler, vim-common
BuildRequires:   curl-devel, openssl-devel, wayland-devel, libaom-devel, opus-devel, libzip-devel, libzstd-devel

Requires:       shadow-utils, redhad-lsb-5.0, ebtables-legacy, iproute
Requires:       iptables-legacy, bridge-utils, dnsmasq, libfdt, e2fsprogs, ebtables, iptables, bsdtar
Requires:       libcurl, libdrm, mesa-libGL, libusb, libXext, net-tools, openssl, python3, util-linux
Requires:       curl >= 7.63.0, glibc >= 2.34, libgcc >= 3.0, libstdc++ >= 11
Requires:       fmt-devel, gflags-devel, jsoncpp-devel, protobuf-devel, openssl-devel, libxml2-devel
#Requires:      f2fs-tools, libx11-6, libz3-4
Requires:       wayland-utils

%description
Cuttlefish Android Virtual Device
Contains set of tools and binaries required to boot up and manage
Cuttlefish Android Virtual Device that are used in all deployments.

%prep
%define workdir `pwd`


%build
# WARNING: For repository 'zlib', the root module requires module version zlib@1.3.1.bcr.3, but got zlib@1.3.1.bcr.4 in the resolved dependency graph.
if [ -f /home/runner/patch_zlib.sh ] && /home/runner/patch_zlib.sh

cd ../../../base/cvd && bazel build --ui_event_filters=-INFO --verbose_failures ...

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/lib/cuttlefish-common/bin
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/rc.d/init.d
mkdir -p %{buildroot}/etc/NetworkManager/conf.d
mkdir -p %{buildroot}/etc/modules-load.d
mkdir -p %{buildroot}/etc/security/limits.d
mkdir -p %{buildroot}/lib/systemd/system
mkdir -p %{buildroot}/lib/udev/rules.d/

%define srcpath ../../../base/host/packages/cuttlefish-base
install -m 655 %{srcpath}/etc/NetworkManager/conf.d/99-cuttlefish.conf %{buildroot}/etc/NetworkManager/conf.d/99-cuttlefish.conf
install -m 655 %{srcpath}/etc/modules-load.d/cuttlefish-common.conf %{buildroot}/etc/modules-load.d/cuttlefish-common.conf
install -m 655 %{srcpath}/etc/security/limits.d/1_cuttlefish.conf %{buildroot}/etc/security/limits.d/1_cuttlefish.conf

%define srcpath ../../../base/debian
install -m 655 %{srcpath}/cuttlefish-base.cuttlefish-host-resources.default %{buildroot}/etc/default/cuttlefish-host-resources
install -m 655 %{srcpath}/cuttlefish-base.cuttlefish-host-resources.init %{buildroot}/etc/rc.d/init.d/cuttlefish-host-resources

%define srcpath ../../../base/rhel
install -m 655 %{srcpath}/cuttlefish.service %{buildroot}/lib/systemd/system/cuttlefish.service

%define srcpath ../../../base/cvd/bazel-bin/cuttlefish/package
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd %{buildroot}/usr/lib/cuttlefish-common/bin/cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/snapshot_util_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/snapshot_util_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/powerwash_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/powerwash_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/powerbtn_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/powerbtn_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/assemble_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/assemble_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/restart_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/restart_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/record_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/record_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/run_cvd %{buildroot}/usr/lib/cuttlefish-common/bin/run_cvd
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_update_security_algorithm %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_update_security_algorithm
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_send_id_disclosure %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_send_id_disclosure
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_internal_display %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_internal_display
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_import_locations %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_import_locations
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_internal_status %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_internal_status
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_internal_start %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_internal_start
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_internal_stop %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_internal_stop
install -m 655 %{srcpath}/cuttlefish-common/bin/cvd_internal_env %{buildroot}/usr/lib/cuttlefish-common/bin/cvd_internal_env
install -m 655 %{srcpath}/cuttlefish-common/bin/echo_server %{buildroot}/usr/lib/cuttlefish-common/bin/echo_server
install -m 655 %{srcpath}/cuttlefish-common/bin/openwrt_control_server %{buildroot}/usr/lib/cuttlefish-common/bin/openwrt_control_server
install -m 655 %{srcpath}/cuttlefish-common/bin/screen_recording_server %{buildroot}/usr/lib/cuttlefish-common/bin/screen_recording_server
install -m 655 %{srcpath}/cuttlefish-common/bin/control_env_proxy_server %{buildroot}/usr/lib/cuttlefish-common/bin/control_env_proxy_server
install -m 655 %{srcpath}/cuttlefish-common/bin/socket_vsock_proxy %{buildroot}/usr/lib/cuttlefish-common/bin/socket_vsock_proxy
install -m 655 %{srcpath}/cuttlefish-common/bin/gnss_grpc_proxy %{buildroot}/usr/lib/cuttlefish-common/bin/gnss_grpc_proxy
install -m 655 %{srcpath}/cuttlefish-common/bin/operator_proxy %{buildroot}/usr/lib/cuttlefish-common/bin/operator_proxy
install -m 655 %{srcpath}/cuttlefish-common/bin/modem_simulator %{buildroot}/usr/lib/cuttlefish-common/bin/modem_simulator
install -m 655 %{srcpath}/cuttlefish-common/bin/tcp_connector %{buildroot}/usr/lib/cuttlefish-common/bin/tcp_connector
install -m 655 %{srcpath}/cuttlefish-common/bin/adb_connector %{buildroot}/usr/lib/cuttlefish-common/bin/adb_connector
install -m 655 %{srcpath}/cuttlefish-common/bin/logcat_receiver %{buildroot}/usr/lib/cuttlefish-common/bin/logcat_receiver
install -m 655 %{srcpath}/cuttlefish-common/bin/tombstone_receiver %{buildroot}/usr/lib/cuttlefish-common/bin/tombstone_receiver
install -m 655 %{srcpath}/cuttlefish-common/bin/console_forwarder %{buildroot}/usr/lib/cuttlefish-common/bin/console_forwarder
install -m 655 %{srcpath}/cuttlefish-common/bin/acloud_translator %{buildroot}/usr/lib/cuttlefish-common/bin/acloud_translator
install -m 655 %{srcpath}/cuttlefish-common/bin/kernel_log_monitor %{buildroot}/usr/lib/cuttlefish-common/bin/kernel_log_monitor
install -m 655 %{srcpath}/cuttlefish-common/bin/process_restarter %{buildroot}/usr/lib/cuttlefish-common/bin/process_restarter
install -m 655 %{srcpath}/cuttlefish-common/bin/graphics_detector %{buildroot}/usr/lib/cuttlefish-common/bin/graphics_detector
install -m 655 %{srcpath}/cuttlefish-common/bin/generate_shader_embed %{buildroot}/usr/lib/cuttlefish-common/bin/generate_shader_embed
install -m 655 %{srcpath}/cuttlefish-common/bin/mkenvimage_slim %{buildroot}/usr/lib/cuttlefish-common/bin/mkenvimage_slim
install -m 655 %{srcpath}/cuttlefish-common/bin/extract-ikconfig %{buildroot}/usr/lib/cuttlefish-common/bin/extract-ikconfig
install -m 655 %{srcpath}/cuttlefish-common/bin/extract-vmlinux %{buildroot}/usr/lib/cuttlefish-common/bin/extract-vmlinux
install -m 655 %{srcpath}/cuttlefish-common/bin/allocd_client %{buildroot}/usr/lib/cuttlefish-common/bin/allocd_client
install -m 655 %{srcpath}/cuttlefish-common/bin/metrics_launcher %{buildroot}/usr/lib/cuttlefish-common/bin/metrics_launcher
install -m 655 %{srcpath}/cuttlefish-common/bin/metrics %{buildroot}/usr/lib/cuttlefish-common/bin/metrics
install -m 655 %{srcpath}/cuttlefish-common/bin/log_tee %{buildroot}/usr/lib/cuttlefish-common/bin/log_tee
install -m 655 %{srcpath}/cuttlefish-common/bin/health %{buildroot}/usr/lib/cuttlefish-common/bin/health


%define srcpath ../../../base/host/deploy
install -m 655 %{srcpath}/install_zip.sh %{buildroot}/usr/bin/install_zip.sh
install -m 655 %{srcpath}/unpack_boot_image.py %{buildroot}/usr/lib/cuttlefish-common/bin/unpack_boot_image.py
install -m 655 %{srcpath}/capability_query.py %{buildroot}/usr/lib/cuttlefish-common/bin/capability_query.py

%define srcpath ../../../base/debian
install -m 655 %{srcpath}/cuttlefish-integration.udev %{buildroot}/lib/udev/rules.d/60-cuttlefish-integration.rules

%post
ln -sf /usr/lib/cuttlefish-common/bin/cvd /usr/bin/cvd
getent group cvdnetwork > /dev/null 2>&1 || groupadd --system cvdnetwork
udevadm control --reload-rules && udevadm trigger
systemctl restart NetworkManager
systemctl daemon-reload
systemctl start cuttlefish

%preun
systemctl stop cuttlefish
rm /usr/bin/cvd


%postun
udevadm control --reload-rules && udevadm trigger
systemctl restart NetworkManager
systemctl daemon-reload
if getent group cvdnetwork > /dev/null 2>&1 ; then
    groupdel cvdnetwork
fi


%files
/etc/default/cuttlefish-host-resources
/etc/rc.d/init.d/cuttlefish-host-resources
/etc/NetworkManager/conf.d/99-cuttlefish.conf
/etc/modules-load.d/cuttlefish-common.conf
/etc/security/limits.d/1_cuttlefish.conf
/usr/bin/install_zip.sh
/lib/systemd/system/cuttlefish.service
/usr/lib/cuttlefish-common/bin/cvd
/usr/lib/cuttlefish-common/bin/unpack_boot_image.py
/usr/lib/cuttlefish-common/bin/capability_query.py
/lib/udev/rules.d/60-cuttlefish-integration.rules

#%%license add-license-file-here

#%%doc add-docs-here

%changelog
* Sat Apr 12 2025 Martin Zeitler <syslogic@users.noreply.github.com>
- Initial version, including GitHub workflow, which builds these.
