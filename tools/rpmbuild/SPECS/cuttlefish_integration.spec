Name:           cuttlefish_integration
Version:        0.9.29
Release:        1%{?dist}
Summary:        Virtual Device for Android host-side utilities

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish      
#Source0:        cuttlefish_base.tar.gz

BuildArch:      noarch
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

#BuildRequires:  
Requires:       qemu-kvm, cuttlefish_base

%description


%prep


%build


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/modprobe.d
mkdir -p %{buildroot}/etc/rsyslog.d
mkdir -p %{buildroot}/etc/ssh
mkdir -p %{buildroot}/lib/udev/rules.d
mkdir -p %{buildroot}/usr/share/doc/cuttlefish-integration

%define srcpath ../../../base/host/packages/cuttlefish-integration
install -m 655 %{srcpath}/etc/default/instance_configs.cfg.template %{buildroot}/etc/default/instance_configs.cfg.template
install -m 655 %{srcpath}/etc/modprobe.d/cuttlefish-integration.conf %{buildroot}/etc/modprobe.d/cuttlefish-integration.conf
install -m 655 %{srcpath}/etc/rsyslog.d/91-cuttlefish.conf %{buildroot}/etc/rsyslog.d/91-cuttlefish.conf
install -m 655 %{srcpath}/etc/ssh/sshd_config.cuttlefish %{buildroot}/etc/ssh/sshd_config.cuttlefish

%define srcpath ../../../base/debian
install -m 655 %{srcpath}/cuttlefish-integration.udev %{buildroot}/lib/udev/rules.d/60-cuttlefish-integration.rules

# install -m 655 %{srcpath}/usr/share/doc/cuttlefish-integration/changelog.gz %{buildroot}/usr/share/doc/cuttlefish-integration/changelog.gz
# install -m 655 %{srcpath}/usr/share/doc/copyright %{buildroot}/usr/share/doc/copyright


%post
getent group cvdnetwork || groupadd cvdnetwork
udevadm control --reload-rules && udevadm trigger


%files
/etc/default/instance_configs.cfg.template
/etc/modprobe.d/cuttlefish-integration.conf
/etc/rsyslog.d/91-cuttlefish.conf
/etc/ssh/sshd_config.cuttlefish
/lib/udev/rules.d/60-cuttlefish-integration.rules

#%%license add-license-file-here
#%%doc add-docs-here


%changelog
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

