Name:           cuttlefish-user
Version:        1.4.0
Release:        1%{?dist}
Summary:        Contains the host signaling server supporting multi-device flows over WebRTC.

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish

BuildArch:      x86_64 aarch64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  golang-bin
Requires:       cuttlefish-base, shadow-utils, openssl

%description
Cuttlefish Android Virtual Device companion package
Contains the host signaling server supporting multi-device flows over WebRTC.


%prep


%build
cd ../../../frontend
./build-webui.sh
cd src/host_orchestrator
go build
cd ../operator
go build

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/rc.d/init.d
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/lib/cuttlefish-common/bin
mkdir -p %{buildroot}/usr/share/cuttlefish-common/operator/intercept/js
mkdir -p %{buildroot}/usr/share/cuttlefish-common/operator/static


%define srcpath ../../../frontend/src
install -m 755 %{srcpath}/host_orchestrator/host_orchestrator %{buildroot}/usr/lib/cuttlefish-common/bin/host_orchestrator
install -m 755 %{srcpath}/operator/operator %{buildroot}/usr/lib/cuttlefish-common/bin/operator
# install -m 655 %{srcpath}/operator/intercept/js/server_connector.d.ts %{buildroot}/usr/share/cuttlefish-common/operator/intercept/js/server_connector.d.ts
install -m 655 %{srcpath}//operator/intercept/js/server_connector.js %{buildroot}/usr/share/cuttlefish-common/operator/intercept/js/server_connector.js

%define srcpath ../../../frontend/src/operator/webui/dist/static
for filename in $(ls %{srcpath}) ; do
  install -m 644 %{srcpath}/$filename %{buildroot}/usr/share/cuttlefish-common/operator/static/$filename
done


%files
/usr/lib/cuttlefish-common/bin/host_orchestrator
/usr/lib/cuttlefish-common/bin/operator
# /usr/share/cuttlefish-common/operator/intercept/js/server_connector.d.ts
/usr/share/cuttlefish-common/operator/intercept/js/server_connector.js

/usr/share/cuttlefish-common/operator/static/index.html
/usr/share/cuttlefish-common/operator/static/3rdpartylicenses.txt
/usr/share/cuttlefish-common/operator/static/main.*
/usr/share/cuttlefish-common/operator/static/polyfills.*
/usr/share/cuttlefish-common/operator/static/runtime.*
/usr/share/cuttlefish-common/operator/static/styles.*

#%%license add-license-file-here
#%%doc add-docs-here

%post
ln -sf /usr/lib/cuttlefish-common/bin/host_orchestrator /usr/bin/cvd_host_orchestrator

# The cvdnetwork group is created by cuttlefish-base
if ! getent passwd _cutf-operator > /dev/null 2>&1 ; then
    adduser --system --shell /sbin/nologin --home /var/empty --no-create-home --gid cvdnetwork _cutf-operator
fi

%preun
if [ -f /usr/bin/cvd_host_orchestrator ]; then
    rm /usr/bin/cvd_host_orchestrator
fi


%postun
if getent passwd _cutf-operator > /dev/null 2>&1; then
    userdel _cutf-operator
fi


%changelog
* Sat Apr 12 2025 Martin Zeitler <syslogic@users.noreply.github.com>
- Initial version, including GitHub workflow, which builds these.
