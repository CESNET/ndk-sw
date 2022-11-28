Summary: A helper package for build with cmake3 on EPEL7
Name: cmake3-links
Version: 1.0
Release: 1
License: GPL
Packager: Martin Spinler <spinler@cesnet.cz>

%description
Use on EPEL7 system, which doesn't have cmake/cpack binary for
programs using hardcoded names for cmake.

%install
mkdir %{buildroot}%{_bindir} -p

ln -s cmake %{buildroot}%{_bindir}/cmake3
ln -s cpack %{buildroot}%{_bindir}/cpack3

%files
%{_bindir}/cmake3
%{_bindir}/cpack3
