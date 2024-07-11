Name:           cuttlefish_user
Version:        0.9.29
Release:        1%{?dist}
Summary:        Virtual Device for Android host-side utilities

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish      
#Source0:        cuttlefish_frontend.tar.gz

BuildArch:      x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

#BuildRequires:  
Requires:       cuttlefish_base

%description


%prep
#%%autosetup -v


%build
cd ../../../frontend
./build-webui.sh


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p %{buildroot}/etc/default
mkdir -p %{buildroot}/etc/init.d
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/lib/cuttlefish-common/bin
mkdir -p %{buildroot}/usr/share/cuttlefish-common/operator/intercept
mkdir -p %{buildroot}/usr/share/cuttlefish-common/operator/static
mkdir -p %{buildroot}/usr/share/doc/cuttlefish-user

%define srcpath ../../../frontend/src/operator/webui/dist/static
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/usr/share/cuttlefish-common/operator/static/$filename
done


%files
/usr/share/cuttlefish-common/operator/static/index.html
/usr/share/cuttlefish-common/operator/static/3rdpartylicenses.txt
/usr/share/cuttlefish-common/operator/static/main.*
/usr/share/cuttlefish-common/operator/static/polyfills.*
/usr/share/cuttlefish-common/operator/static/runtime.*
/usr/share/cuttlefish-common/operator/static/styles.*

#%%license add-license-file-here
#%%doc add-docs-here


%changelog
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

