Summary: NFS Mountpoint Check
Name: nfs-mountpoint-check
Version: 1.1
Release: 1%{?dist}
License: MIT
Group: Applications/System
URL: https://github.com/LCOGT/nfs-mountpoint-check
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
This application is the Boltwood Cloud Interface (BCI). The application
connects to a virtual serial port to communicate with a Boltwood Cloud Sensor,
and outputs a network weather file suitable for serving to the WMS/RCS. 

%prep
%setup -q

%build
CFLAGS="${CFLAGS:-%optflags}" ; export CFLAGS ; 
%{?__global_ldflags:LDFLAGS="${LDFLAGS:-%__global_ldflags}" ; export LDFLAGS ;}
make CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS"

%install
rm -rf %{buildroot}

%{__install} -d -m 0755 %{buildroot}%{_bindir}
%{__install} -p -m 0755 %{name} %{buildroot}%{_bindir}/%{name}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}

%changelog
* Mon Jan 14 2019 Ira W. Snyder <isnyder@lco.global> - 1.0-1
- Initial package.
