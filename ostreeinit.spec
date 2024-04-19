Name:           ostreeinit
Version:        0.1.0
Release:        1%{?dist}
Summary:        Minimal ostree-based initrd

License:        MIT
Source0:        ostreeinit-%{version}.tar.xz

BuildRequires:  meson
BuildRequires:  gcc
Requires:       ostree

%description
Minimal ostree-based initrd

%prep
%autosetup

%build
%meson
%meson_build

%check
%meson_test

%install
%meson_install

%files
%{_prefix}/lib/dracut/modules.d/50ostreeinit
%{_prefix}/lib/ostreeinit/ostreeinit

%changelog
* Thu Apr 18 2024 Alexander Larsson <alexl@redhat.com>
- Initial version
