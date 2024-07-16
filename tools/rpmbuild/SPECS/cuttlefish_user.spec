Name:           cuttlefish-user
Version:        0.9.29
Release:        1%{?dist}
Summary:        Virtual Device for Android host-side utilities

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish      
#Source0:        cuttlefish_frontend.tar.gz

BuildArch:      x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

#BuildRequires:  
Requires:       openssl, cuttlefish-base

%description


%prep
#%%autosetup -v


%build
cd ../../../frontend
./build-webui.sh

cd ../base/cvd
bazel query ...
bazel build cuttlefish:cuttlefish_common --spawn_strategy=local

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/rc.d/init.d
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/lib/cuttlefish-common/bin
mkdir -p %{buildroot}/usr/share/cuttlefish-common/operator/intercept
mkdir -p %{buildroot}/usr/share/cuttlefish-common/operator/static
mkdir -p %{buildroot}/usr/share/doc/cuttlefish-user

%define srcpath ../../../frontend/src/operator/webui/dist/static
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/usr/share/cuttlefish-common/operator/static/$filename
done

%define srcpath ../../../frontend/src/host_orchestrator/host_orchestrator
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/usr/lib/cuttlefish-common/bin/$filename
done

%define srcpath ../../../frontend/src/operator/operator
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/usr/lib/cuttlefish-common/bin/$filename
done

%define srcpath ../../../frontend/src/operator/intercept
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/usr/share/cuttlefish-common/operator/$filename
done

%define srcpath ../../../frontend/src/operator/webui/dist/static
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/usr/share/cuttlefish-common/operator/static/$filename
done


%files
/usr/bin/cvd_host_orchestrator
/usr/lib/cuttlefish-common/bin/host_orchestrator
/usr/lib/cuttlefish-common/bin/orchestrator
/usr/share/cuttlefish-common/operator/static/index.html
/usr/share/cuttlefish-common/operator/static/3rdpartylicenses.txt
/usr/share/cuttlefish-common/operator/static/main.*
/usr/share/cuttlefish-common/operator/static/polyfills.*
/usr/share/cuttlefish-common/operator/static/runtime.*
/usr/share/cuttlefish-common/operator/static/styles.*

#/usr/share/doc/cuttlefish-user/changelog.gz
#/usr/share/doc/cuttlefish-user/copyright

#%%license add-license-file-here
#%%doc add-docs-here

%post
if ! getent passwd _cutf-operator > /dev/null 2>&1 then
    # The cvdnetwork group is created by cuttlefish-base
    adduser --system --disabled-password --disabled-login --home /var/empty \
    --no-create-home --quiet --force-badname --ingroup cvdnetwork _cutf-operator
fi

%postun
userdel _cutf-operator

%changelog
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

