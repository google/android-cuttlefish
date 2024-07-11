Name:           cuttlefish_user
Version:        0.9.29
Release:        1%{?dist}
Summary:        Virtual Device for Android host-side utilities

License:        Apache License 2.0
URL:            https://github.com/google/android-cuttlefish      
#Source0:        cuttlefish_frontend.tar.gz

BuildArch:      noarch
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
mkdir -p %{buildroot}/var/www/local.cuttlefish

%define srcpath ../../../frontend/src/operator/webui/dist/static
for filename in $(ls %{srcpath}) ; do
  install -m 655 %{srcpath}/$filename %{buildroot}/var/www/local.cuttlefish/$filename
done


%files
/var/www/local.cuttlefish/index.html
/var/www/local.cuttlefish/3rdpartylicenses.txt
/var/www/local.cuttlefish/main.*
/var/www/local.cuttlefish/polyfills.*
/var/www/local.cuttlefish/runtime.*
/var/www/local.cuttlefish/styles.*

#%%license add-license-file-here
#%%doc add-docs-here


%changelog
* Thu Jul 11 2024 Martin Zeitler <?>
- Initial version.

