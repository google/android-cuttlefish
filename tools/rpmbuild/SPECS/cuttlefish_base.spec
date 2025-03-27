Name:           cuttlefish-base
Version:        0.9.29
Release:        1%{?dist}
Summary:        Cuttlefish Android Virtual Device

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish

BuildArch:      x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:  curl-devel, openssl-devel, protobuf-devel, protobuf-compiler

Requires:       shadow-utils, redhat-lsb-core, ebtables-legacy, iproute
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
cd ../../../base/cvd
# $HOME/go/bin/bazelisk build cuttlefish:cvd --spawn_strategy=local
$HOME/go/bin/bazelisk build //cuttlefish/package:cvd --spawn_strategy=local


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

%define srcpath ../../../base/cvd/bazel-bin
install -m 755 %{srcpath}/cuttlefish/cvd %{buildroot}/usr/lib/cuttlefish-common/bin/cvd

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
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

