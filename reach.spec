Name:           reach
Version:        0.1.0
Release:        1%{?dist}
%global debug_package %{nil}
Summary:        Cached reachability query for DMA-BUF buffers on i915

License:        MIT
URL:            https://github.com/SimoesCTT/reach
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  opencl-headers
BuildRequires:  OpenCL-ICD-Loader-devel

%description
reach answers one question cheaply: can this device reach this
chipmem buffer, and will the access be zero-copy or copied?

DRM_IOCTL_PRIME_FD_TO_HANDLE costs ~7 us per call on Skylake-U /
kernel 7.2.13, warm or cold, and returns a GEM handle whether the
subsequent access will be zero-copy or copied. reach caches the
reachability answer per (buffer, device) pair and records the cost
class once, so a hot path pays the import once (~7 us) instead of per
call. Measured cached cost: ~43 ns per query.

Not a restored kernel primitive: the import ioctl is the reachability
test. reach adds a cache, because the ioctl is expensive, and a
cost-class answer, because the ioctl cannot give one.


%prep
%autosetup


%build
%make_build


%check
if [ -e /dev/dri/renderD128 ]; then
    make run-test
else
    echo "SKIP: no /dev/dri/renderD128 in buildroot; the reach test requires a render node"
fi


%install
install -d %{buildroot}%{_includedir}/reach
install -m 0644 include/reach.h %{buildroot}%{_includedir}/reach/


%files
%{_includedir}/reach
%license LICENSE
%doc README.md
%doc docs/audit/0001-reachability.md


%changelog
* Wed Nov 12 2026 Americo Simoes <amexsimoes@gmail.com> - 0.1.0-1
- Initial package.
