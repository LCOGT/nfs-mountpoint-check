Summary: NFS Mountpoint Check
Name: nfs-mountpoint-check
Version: 1.3
Release: 1%{?dist}
License: MIT
Group: Applications/System
URL: https://github.com/LCOGT/nfs-mountpoint-check
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
NFS Mountpoint Check utility.

%prep
%setup -q

%build
CFLAGS="${CFLAGS:-%optflags}"
CFLAGS="${CFLAGS} $(getconf LFS_CFLAGS)"

%{?__global_ldflags:LDFLAGS="${LDFLAGS:-%__global_ldflags}"}
LDFLAGS="${LDFLAGS} $(getconf LFS_LDFLAGS)"

export CFLAGS
export LDFLAGS

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
* Thu Feb 28 2019 Ira W. Snyder <isnyder@lco.global> - 1.3-1
- Add Large File Support to avoid EOVERFLOW errors.
* Mon Jan 14 2019 Ira W. Snyder <isnyder@lco.global> - 1.0-1
- Initial package.
