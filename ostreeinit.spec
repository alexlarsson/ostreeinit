Name:           ostreeinit
Version:        0.1
Release:        1%{?dist}
Summary:        Minimal ostree-based initrd

License:        MIT
Source0:        ostreeinit-%{version}.tar.gz

BuildRequires:  gcc
Requires:       ostree

%description
Minimal ostree-based initrd

%prep
%autosetup


%build
%make_build

%install
%make_install

%files
%{_prefix}/lib/dracut/modules.d/50ostreeinit
%{_prefix}/lib/ostreeinit/ostreeinit

%changelog
* Thu Apr 18 2024 Alexander Larsson <alexl@redhat.com>
- Initial version
