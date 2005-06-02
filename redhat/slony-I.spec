%{!?perltools:%define perltools 1}
%{!?docs:%define docs 0}

Summary:	A "master to multiple slaves" replication system with cascading and failover.
Name: 		slony1
Version: 	1.1.0
Release: 	beta3
License: 	Berkeley/BSD
Group: 		Applications/Databases
URL: 		http://slony.info/
Packager: 	Devrim Gunduz <devrim@PostgreSQL.org>
Source0: 	slony1-%{version}.%{release}.tar.gz
Buildroot: 	%{_tmppath}/%{name}-%{version}-root
BuildRequires: 	postgresql-devel
Requires:	postgresql-server

%description
Slony-I will be a "master to multiple slaves" replication 
system with cascading and failover.

The big picture for the development of Slony-I is to build 
a master-slave system that includes all features and 
capabilities needed to replicate large databases to a 
reasonably limited number of slave systems.

Slony-I is planned as a system for data centers and backup 
sites, where the normal mode of operation is that all nodes 
are available

%prep
%setup -q -n slony1-%{version}.%{release}

%build
autoconf
CFLAGS="${CFLAGS:-%optflags}" ; export CFLAGS
CXXFLAGS="${CXXFLAGS:-%optflags}" ; export CXXFLAGS
CPPFLAGS="${CPPFLAGS} -I%{_includedir}/et" ; export CPPFLAGS
CFLAGS="${CFLAGS} -I%{_includedir}/et" ; export CFLAGS

# Strip out -ffast-math from CFLAGS....

CFLAGS=`echo $CFLAGS|xargs -n 1|grep -v ffast-math|xargs -n 100`
export LIBNAME=%{_lib}

./configure --bindir /usr/bin --libdir %{_libdir}/pgsql \
        --includedir %{_includedir}/pgsql \
%if %perltools
        --with-perltools=%{_bindir} \
%endif
%if %docs
        --with-docs \
%endif
        --datadir %{_datadir}/pgsql --sysconfdir=/etc --with-pglibdir=%{_libdir}/pgsql --with-docdir=/usr/share/doc 
make
%if %perltools
 cd tools
 make
%endif

%install
rm -rf $RPM_BUILD_ROOT
install -d $RPM_BUILD_ROOT%{_sysconfdir}
install -d $RPM_BUILD_ROOT%{_datadir}/pgsql/
install -d $RPM_BUILD_ROOT%{_libdir}/pgsql/
make DESTDIR=$RPM_BUILD_ROOT install
install -m 0755 src/backend/slony1_funcs.so $RPM_BUILD_ROOT%{_libdir}/pgsql/slony1_funcs.so
install -m 0755 src/xxid/xxid.so $RPM_BUILD_ROOT%{_libdir}/pgsql/xxid.so
install -m 0755 src/backend/*.sql $RPM_BUILD_ROOT%{_datadir}/pgsql/
install -m 0755 src/xxid/*.sql $RPM_BUILD_ROOT%{_datadir}/pgsql/
install -m 0755 tools/*.sh  $RPM_BUILD_ROOT%{_bindir}/
install -m 0755 share/slon.conf-sample $RPM_BUILD_ROOT%{_sysconfdir}/slon.conf

%if %perltools
cd tools
make DESTDIR=$RPM_BUILD_ROOT install
/bin/rm -rf altperl/*.pl altperl/ToDo altperl/README altperl/Makefile altperl/CVS
install -m 0755 altperl/slon_tools.conf-sample  
$RPM_BUILD_ROOT%{_sysconfdir}/slon_tools.conf
install -m 0755 altperl/* $RPM_BUILD_ROOT%{_bindir}/
install -m 0755 altperl/slon-tools.pm  $RPM_BUILD_ROOT%{_libdir}/pgsql/
/bin/rm -f  $RPM_BUILD_ROOT%{_sysconfdir}/slon_tools.conf-sample
/bin/rm -f  $RPM_BUILD_ROOT%{_bindir}/slon_tools.conf-sample
/bin/rm -f $RPM_BUILD_ROOT%{_libdir}/slon-tools.pm
/bin/rm -f $RPM_BUILD_ROOT%{_bindir}/slon-tools.pm
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%if %docs
%doc COPYRIGHT doc/adminguide  doc/concept  doc/howto  doc/implementation  doc/support
%endif
%{_bindir}/*
%{_libdir}/pgsql/slony1_funcs.so
%{_libdir}/pgsql/xxid.so
%{_datadir}/pgsql/*.sql
%{_sysconfdir}/slon.conf
%if %perltools
%{_libdir}/pgsql/slon-tools.pm
%{_sysconfdir}/slon_tools.conf
%endif

%changelog
* Thu Jun 02 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- Apply a new %docs macro and disable building of docs by default.
- Remove slon-tools.conf-sample from bindir.

* Mon Apr 10 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- More fixes on RPM builds

* Thu Apr 07 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- More fixes on RPM builds

* Thu Apr 04 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- Fix RPM build errors, regarding to tools/ .

* Thu Apr 02 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- Added docs to installed files list.
- Fixed doc problems
- Updated the spec file

* Thu Mar 17 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- Update to 1.1.0beta1
- Remove PostgreSQL source dependency

* Thu Mar 17 2005 Devrim Gunduz <devrim@PostgreSQL.org> postgresql-slony1-engine
- Fix RPM builds

* Thu Mar 18 2004 Daniel Berrange <berrange@redhat.com> postgresql-slony1-engine
- Initial RPM packaging
