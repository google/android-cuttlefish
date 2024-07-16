Name:           cuttlefish-orchestration
Version:        0.9.29
Release:        1%{?dist}
Summary:        Virtual Device for Android host-side utilities

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish      

BuildArch:      x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

#BuildRequires:  
Requires:       nginx, openssl, bash, systemd-journal-remote, cuttlefish-base, cuttlefish-user

%description


%prep


%build


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/nginx/conf.d
mkdir -p %{buildroot}/etc/rc.d/init.d
mkdir -p %{buildroot}/etc/sudoers.d
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/doc/cuttlefish-orchestration

%define srcpath ../../../frontend/host/packages/cuttlefish-orchestration
install -m 655 %{srcpath}/etc/nginx/conf.d/cuttlefish-orchestration.conf %{buildroot}/etc/nginx/conf.d/cuttlefish-orchestration.conf
install -m 655 %{srcpath}/etc/sudoers.d/cuttlefish-orchestration %{buildroot}/etc/sudoers.d/cuttlefish-orchestration


%files
/etc/nginx/conf.d/cuttlefish-orchestration.conf
/etc/sudoers.d/cuttlefish-orchestration

#%%license add-license-file-here
#%%doc add-docs-here


%post
# The cvdnetwork group is created by cuttlefish-base
if ! getent passwd _cvd-executor > /dev/null 2>&1; then
    adduser --system --home /var/empty --no-create-home _cvd-executor
    # getent group _cvd-executor || groupadd _cvd-executor
    usermod -a -G cvdnetwork,kvm _cvd-executor
fi

# Reload nginx having the orchestration configuration
# service nginx reload

%preun


%postun
if getent passwd _cvd-executor > /dev/null 2>&1; then
    userdel _cvd-executor
    groupdel _cvd-executor
fi


# Reload nginx without the orchestration configuration
# service nginx reload

%changelog
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

