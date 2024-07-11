Name:           cuttlefish_base
Version:        0.9.29
Release:        1%{?dist}
Summary:        Virtual Device for Android host-side utilities

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish      
#Source0:        cuttlefish_base.tar.gz

BuildArch:      x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  curl-devel
Requires:       adduser, bridge-utils, curl, dnsmasq-base, e2fsprogs, ebtables-legacy / ebtables, f2fs-tools, iproute2, iptables, libarchive-tools / bsdtar, libcurl4,
Requires:       libdrm2, libfdt1, libfmt-devel, libgflags-devel, libgl1, libjsoncpp-devel, libprotobuf-devel, libssl-devel, libusb-1.0-0, libwayland-client0, libwayland-server0,
Requires:       libx11-6, libxext6, libxml2-devel, libz3-4, lsb-base, net-tools, openssl, python3, util-linux

%description


%prep
%define workdir `pwd`

%build
cd ../../../base/cvd
bazel build cuttlefish:cvd --spawn_strategy=local

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/lib/cuttlefish-common/bin
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/NetworkManager/conf.d
mkdir -p %{buildroot}/etc/modules-load.d
mkdir -p %{buildroot}/etc/security/limits.d

%define srcpath ../../../base/host/packages/cuttlefish-base
install -m 655 %{srcpath}/etc/NetworkManager/conf.d/99-cuttlefish.conf %{buildroot}/etc/NetworkManager/conf.d/99-cuttlefish.conf
install -m 655 %{srcpath}/etc/modules-load.d/cuttlefish-common.conf %{buildroot}/etc/modules-load.d/cuttlefish-common.conf
install -m 655 %{srcpath}/etc/security/limits.d/1_cuttlefish.conf %{buildroot}/etc/security/limits.d/1_cuttlefish.conf

%define srcpath ../../../base/debian
install -m 655 %{srcpath}/cuttlefish-base.cuttlefish-host-resources.default %{buildroot}/etc/default/cuttlefish-host-resources

%define srcpath ../../../base/cvd/bazel-bin
install -m 755 %{srcpath}/cuttlefish/cvd %{buildroot}/usr/lib/cuttlefish-common/bin/cvd

%define srcpath ../../../base/host/deploy
install -m 655 %{srcpath}/unpack_boot_image.py %{buildroot}/usr/lib/cuttlefish-common/bin/unpack_boot_image.py
install -m 655 %{srcpath}/capability_query.py %{buildroot}/usr/lib/cuttlefish-common/bin/capability_query.py
install -m 655 %{srcpath}/install_zip.sh %{buildroot}/usr/bin/install_zip.sh


%files
/etc/default/cuttlefish-host-resources
/etc/NetworkManager/conf.d/99-cuttlefish.conf
/etc/modules-load.d/cuttlefish-common.conf
/etc/security/limits.d/1_cuttlefish.conf
/usr/lib/cuttlefish-common/bin/cvd
/usr/lib/cuttlefish-common/bin/unpack_boot_image.py
/usr/lib/cuttlefish-common/bin/capability_query.py
/usr/bin/install_zip.sh

#%%license add-license-file-here
#%%doc add-docs-here


%changelog
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

