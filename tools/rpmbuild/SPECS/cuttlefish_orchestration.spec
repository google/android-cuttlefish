Name:           cuttlefish-orchestration
Version:        1.4.0
Release:        1%{?dist}
Summary:        Contains the host orchestrator.

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish

BuildArch:      x86_64 aarch64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  openssl
Requires:       cuttlefish-base, cuttlefish-user, bash, nginx, openssl, ca-certificates, shadow-utils, systemd-udev, systemd-journal-remote

%description
Cuttlefish Android Virtual Device companion package
Contains the host orchestrator.

%prep


%build
cd ../../../frontend
if [ ! -d .sslcert ]; then
  ./gen_ssl_cert.sh -o .sslcert
fi



%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/cuttlefish-orchestration/ssl/cert
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/nginx/conf.d
mkdir -p %{buildroot}/etc/rc.d/init.d
mkdir -p %{buildroot}/etc/sudoers.d

%define srcpath ../../../frontend/.sslcert
install -m 544 %{srcpath}/cert.pem %{buildroot}/etc/cuttlefish-orchestration/ssl/cert/cert.pem
install -m 544 %{srcpath}/key.pem %{buildroot}/etc/cuttlefish-orchestration/ssl/cert/key.pem

%define srcpath ../../../frontend/host/packages/cuttlefish-orchestration
install -m 544 %{srcpath}/etc/nginx/conf.d/cuttlefish-orchestration.conf %{buildroot}/etc/nginx/conf.d/cuttlefish-orchestration.conf
install -m 544 %{srcpath}/etc/sudoers.d/cuttlefish-orchestration %{buildroot}/etc/sudoers.d/cuttlefish-orchestration


%files
/etc/cuttlefish-orchestration/ssl/cert/cert.pem
/etc/cuttlefish-orchestration/ssl/cert/key.pem
/etc/nginx/conf.d/cuttlefish-orchestration.conf
/etc/sudoers.d/cuttlefish-orchestration

#%%license add-license-file-here

#%%doc add-docs-here

%post
ln -sf /usr/lib/cuttlefish-common/bin/cvd /usr/bin/fetch_cvd

# The cvdnetwork group is created by cuttlefish-base
if ! getent passwd _cvd-executor > /dev/null 2>&1 ; then
    adduser --system --shell /sbin/nologin --home /var/empty --no-create-home _cvd-executor
    usermod -a -G cvdnetwork,kvm _cvd-executor
fi

# Reload nginx having the orchestration configuration
systemctl try-reload-or-restart nginx.service


%preun


%postun
if [ -f /usr/bin/fetch_cvd ]; then
    rm /usr/bin/fetch_cvd
fi

if getent passwd _cvd-executor > /dev/null 2>&1; then
    userdel _cvd-executor
fi

# Reload nginx without the orchestration configuration
systemctl try-reload-or-restart nginx.service

%changelog
* Sat Apr 12 2025 Martin Zeitler <syslogic@users.noreply.github.com>
- Initial version, including GitHub workflow, which builds these.
