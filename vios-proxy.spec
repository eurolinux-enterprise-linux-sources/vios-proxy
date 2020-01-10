Name:           vios-proxy
Version:        0.2
Release:        1%{?dist}
Summary:        Network proxy between a QEMU host and QEMU guests using virtioserial channels

Group:          System Environment/Daemons
License:        ASL 2.0
URL:            http://git.fedorahosted.org/git/?p=vios-proxy.git
Source0:        http://fedorahosted.org/released/vios-proxy/%{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  boost-devel 
BuildRequires:  gcc-c++ 
BuildRequires:  cmake >= 2.6.0

%description
The vios-proxy program suite creates a network tunnel between
a server in the QEMU host and a client in a QEMU guest.
The proxy server and client programs open normal TCP network
ports on localhost and the vios-proxy tunnel connects them using
QEMU virtioserial channels.

%package host

Summary:        Network proxy using virtioserial for QEMU host
Group:          System Environment/Daemons

%description host
The vios-proxy-host daemon runs on a QEMU host. A vios-proxy-host daemon
manages all the proxy connections for a single proxy service on the host.
Multiple vios-proxy-host daemons are required to provide proxy access to
multiple services on the host. A single vios-proxy-host daemon may open
multiple proxy channels to multiple QEMU guests limited only by the
number of virtioserial connections available to each guest.

%package guest

Summary:        Network proxy using virtioserial for QEMU guest
Group:          System Environment/Daemons

%description guest
The vios-proxy-guest daemon runs on a QEMU client. A vios-proxy-guest daemon
creates a listening network socket on the guest's localhost interface. When
client programs connect to this socket then the vios-proxy-guest daemon opens
a proxy channel to the host through the tunnel.

%package doc

Summary:        Documentation for vios-proxy
Group:          System Environment/Daemons

%description doc
The vios-proxy program suite creates a network tunnel between
a server in the QEMU host and a client in a QEMU guest.
The proxy server and client programs open normal TCP network
ports on localhost and the vios-proxy tunnel connects them using
QEMU virtioserial channels.

%prep
%setup -q
pushd src
cmake -D CMAKE_INSTALL_PREFIX:STRING="%{_prefix}" -D CMAKE_CXX_FLAGS:STRING="%{optflags}" .
popd

%build
pushd src
make %{?_smp_mflags}
popd

%install
rm -rf $RPM_BUILD_ROOT
pushd src
make install DESTDIR=$RPM_BUILD_ROOT
popd

%clean
rm -rf $RPM_BUILD_ROOT


%files doc
%defattr(-,root,root,-)
%doc README.txt LICENSE NOTICE
%doc doc/

%files host
%defattr(-,root,root,-)
%{_bindir}/vios-proxy-host
%doc %{_mandir}/man1/vios-proxy-host.1.gz

%files guest
%defattr(-,root,root,-)
%{_bindir}/vios-proxy-guest
%doc %{_mandir}/man1/vios-proxy-guest.1.gz

%changelog
* Mon Jan  9 2012 Ted Ross <tross@redhat.com> 0.2-1
- Related:rhbz#743723

* Thu Sep 29 2011 Ted Ross <tross@redhat.com> 0.1-2
- Port specfile improvements from Fedora

* Mon Sep 19 2011 Chuck Rolke <crolke@redhat.com> 0.1-1
- Initial revision
