Name:           cuttlefish-base
Version:        1.3.0
Release:        1%{?dist}
Summary:        Cuttlefish Android Virtual Device

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish

BuildArch:      x86_64 aarch64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# Note: `ncurses-compat-libs` require EPEL repository and `protobuf` requires CRB repository.
BuildRequires:  go, cmake, gcc-c++, curl-devel, openssl-devel, wayland-devel ncurses-compat-libs, protobuf-devel, protobuf-compiler, vim-common

Requires:       shadow-utils, redhad-lsb-5.0, ebtables-legacy, iproute
Requires:       iptables-legacy, bridge-utils, dnsmasq, libfdt, e2fsprogs, ebtables, iptables, bsdtar
Requires:       libcurl, libdrm, mesa-libGL, libusb, libXext, net-tools, openssl, python3, util-linux
Requires:       curl >= 7.63.0, glibc >= 2.34, libgcc >= 3.0, libstdc++ >= 11
Requires:       fmt-devel, gflags-devel, jsoncpp-devel, protobuf-devel, openssl-devel, libxml2-devel
#Requires:      f2fs-tools, libx11-6, libz3-4
# libwayland-client0, libwayland-server0
Requires:       wayland-utils


%description
Cuttlefish Android Virtual Device
Contains set of tools and binaries required to boot up and manage
Cuttlefish Android Virtual Device that are used in all deployments.

%prep
%define workdir `pwd`


%build
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

# TODO: there are more commands there now.
%define srcpath ../../../base/cvd/bazel-bin/cuttlefish/package
# acloud_translator         cvd_internal_env               health                  process_restarter
# adb_connector             cvd_internal_start             kernel_log_monitor      record_cvd
# allocd_client             cvd_internal_status            log_tee                 restart_cvd
# assemble_cvd              cvd_internal_stop              logcat_receiver         run_cvd
# console_forwarder         cvd_send_id_disclosure         metrics                 screen_recording_server
# control_env_proxy_server  cvd_update_security_algorithm  metrics_launcher        snapshot_util_cvd
# cvd                       echo_server                    mkenvimage_slim         socket_vsock_proxy
# cvd.repo_mapping          extract-ikconfig               modem_simulator         tcp_connector
# cvd.runfiles              extract-vmlinux                openwrt_control_server  tombstone_receiver
# cvd.runfiles_manifest     generate_shader_embed          operator_proxy
# cvd_import_locations      gnss_grpc_proxy                powerbtn_cvd
# cvd_internal_display      graphics_detector              powerwash_cvd
install -m 755 %{srcpath}/cuttlefish-common/bin/cvd %{buildroot}/usr/lib/cuttlefish-common/bin/cvd

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
* Sun Mar 30 2025 Martin Zeitler <syslogic@users.noreply.github.com>
- Initial version.
