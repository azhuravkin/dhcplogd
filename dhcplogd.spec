Name: dhcplogd
Version: 1
Release: 1%{?dist}
Group: Applications/System
Summary: Export of logs dhcp servers to mysql database.
License: GPL
Source0: %{name}-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-root
Requires: pcre mysql
BuildRequires: pcre-devel mysql-devel

%description
The program listens to UDP port, which comes with a
stream of syslog-ng, and exports it to mysql database.

%prep
%setup

%install
make clean all

%{__mkdir_p} $RPM_BUILD_ROOT/etc/rc.d/init.d
%{__mkdir_p} $RPM_BUILD_ROOT%{_sbindir}

install dhcplogd.init $RPM_BUILD_ROOT/etc/rc.d/init.d/dhcplogd
install dhcplogd.conf $RPM_BUILD_ROOT/etc
install -s -m 755 %{name} $RPM_BUILD_ROOT%{_sbindir}

%files
%defattr(-,root,root,-)
%doc INSTALL dhcplogd.sql
%config(noreplace) %attr(0600,root,root) /etc/dhcplogd.conf
%{_sbindir}/%{name}
/etc/rc.d/init.d/%{name}

%clean
[ $RPM_BUILD_ROOT != "/" ] && rm -rf $RPM_BUILD_ROOT

%post
/sbin/chkconfig --add dhcplogd
if [ "$1" != "0" ] ; then
    /sbin/service dhcplogd condrestart >/dev/null 2>&1 || true
fi

%preun
if [ "$1" = "0" ] ; then
    /sbin/service dhcplogd stop >/dev/null 2>&1 || true
    /sbin/chkconfig --del dhcplogd
fi

%changelog
* Fri Jun 03 2011 Juravkin Alexander <juravkin@office.solo.by>
- Build
