#
# spec file for package sbxkb (Version 0.7.7)
#

# norootforbuild     

Name:           sbxkb
%define _prefix  /usr
BuildRequires:  gdk-pixbuf-devel gtk2-devel
License:        GPL v2 or later
Group:          System/X11/Utilities
Version:        0.7.7
Release:	0
Summary:        Simple keyboard indicator
Source0:        %{name}-%{version}.tar.bz2
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Url:            http://sourceforge.net/projects/staybox/

AutoReqProv:    on


%description
Simple keyboard indicator

Author: Tuliakov Yrij
--------

%define INSTALL      install -m755 -s
%define INSTALL_DIR  install -d -m755



%prep  
%setup -q -n %{name}-%{version}


%build
./autogen.sh
./configure --prefix=%{_prefix} LIBS=-lX11
make
strip sbxkb

%install
export DESTDIR=%{buildroot}
make install

%clean
rm -rf %{buildroot}

%post


%preun


%files
%defattr(-, root, root, 0755)
%{_bindir}/%{name}
%doc COPYING 
# AUTHORS ChangeLog INSTALL README
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/*


%changelog

