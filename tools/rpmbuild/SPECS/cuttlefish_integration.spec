Name:           cuttlefish-integration
Version:        1.3.0
Release:        1%{?dist}
Summary:        Contains the host signaling server supporting multi-device flows over WebRTC.

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish

BuildArch:      x86_64 aarch64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

#BuildRequires:  
Requires:       cuttlefish-base, qemu-kvm

%description
Cuttlefish Android Virtual Device companion package
Contains the host signaling server supporting multi-device flows over WebRTC.

%prep


%build


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/modprobe.d
mkdir -p %{buildroot}/etc/rsyslog.d
mkdir -p %{buildroot}/etc/ssh
mkdir -p %{buildroot}/lib/udev/rules.d

%define srcpath ../../../base/host/packages/cuttlefish-integration
install -m 644 %{srcpath}/etc/default/instance_configs.cfg.template %{buildroot}/etc/default/instance_configs.cfg.template
install -m 644 %{srcpath}/etc/modprobe.d/cuttlefish-integration.conf %{buildroot}/etc/modprobe.d/cuttlefish-integration.conf
install -m 644 %{srcpath}/etc/rsyslog.d/91-cuttlefish.conf %{buildroot}/etc/rsyslog.d/91-cuttlefish.conf
install -m 655 %{srcpath}/etc/ssh/sshd_config.cuttlefish %{buildroot}/etc/ssh/sshd_config.cuttlefish

%define srcpath ../../../base/debian
install -m 655 %{srcpath}/cuttlefish-integration.udev %{buildroot}/lib/udev/rules.d/60-cuttlefish-integration.rules


%files
/etc/default/instance_configs.cfg.template
/etc/modprobe.d/cuttlefish-integration.conf
/etc/rsyslog.d/91-cuttlefish.conf
/etc/ssh/sshd_config.cuttlefish
/lib/udev/rules.d/60-cuttlefish-integration.rules

#%%license add-license-file-here

#%%doc add-docs-here

%post
systemctl restart systemd-modules-load.service
systemctl reload rsyslog.service

%preun


%postun
systemctl restart systemd-modules-load.service
systemctl reload rsyslog.service

%changelog
* Sat Apr 12 2025 Martin Zeitler <syslogic@users.noreply.github.com>
- Initial version, including GitHub workflow, which builds these.
