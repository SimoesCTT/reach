# reach — a cached reachability query for DMA-BUF buffers

`reach` answers one question cheaply: **can this device reach this
chipmem buffer, and will the access be zero-copy or copied?**

It is a userspace cache over `DRM_IOCTL_PRIME_FD_TO_HANDLE`, justified
by measurement on Skylake-U / kernel 7.2.13:

| path | cost |
|---|---|
| `PRIME_FD_TO_HANDLE` warm | ~7 µs per call (median 7021–8390 ns) |
| `reach_query` cached | ~43 ns per call (mean 40–43 ns over 1000 runs) |

The ioctl does not cache in the way some sources describe: warm cost is
the same as cold. It also cannot distinguish zero-copy from copied
access — it returns a GEM handle whether the subsequent access is direct
or through a staging buffer. `reach` caches the reachability answer per
`(buffer, device)` pair and records the cost class once, so a hot path
pays the import once instead of per call.

## API

```c
#include "reach.h"

reach_t ans;
int rc = reach_query(buf, device_fd, &ans);   /* first call: one import */
reach_query(buf, device_fd, &ans);            /* subsequent: ~43 ns */

switch (ans) {
case REACH_YES_ZERO_COPY: /* GPU reads/writes the caller's pages */
case REACH_YES_COPY:      /* GPU access goes through a staging buffer */
case REACH_NO:            /* device cannot reach this buffer */
case REACH_UNKNOWN:
}
reach_invalidate(buf, device_fd) drops one cached pair;
reach_reset() clears the cache. Invalidate when a backend gains or
loses zero-copy capability (e.g. Rusticl gaining
cl_khr_external_memory_dma_buf).


What this is not
Not a restored kernel primitive. The reachability primitive already
exists — it is the import ioctl. What reach adds is (a) a cache,
because the ioctl is ~7 µs even warm, and (b) a cost-class answer,
because the ioctl returns a handle without saying whether the access
will be direct or copied.

If a future kernel makes PRIME_FD_TO_HANDLE cheap and
cost-class-observable, reach should be deleted.

Audit
docs/audit/0001-reachability.md — every claim is closed by a
measurement: the ioctl is expensive warm and cold, it does not
distinguish cost class, a cached query is sub-microsecond, and the
cache does not grow per call.

Build
bash
make check      # builds libreach.a and runs tests/t_reach
make run-test   # first query pays the import; cached queries are sub-us
Environment
Fedora 43, kernel 7.2.13, Skylake-U HD 520, i915, /dev/dri/renderD128.
Depends on chipmem (vendored from the-level).


## 4. The spec

`reach` depends on `chipmem`, which is vendored in `vendor/`. Same
problem as `copperlist`: bundling. Same honest handling — ship it,
acknowledge the bundling MUST item, and switch to
`BuildRequires: the-level-devel` once `the-level` is accepted.

```bash
cat > ~/Desktop/reach/reach.spec <<'EOF'
Name:           reach
Version:        0.1.0
Release:        1%{?dist}
%global debug_package %{nil}
Summary:        Cached reachability query for DMA-BUF buffers on i915

License:        MIT
URL:            https://github.com/SimoesCTT/reach
Source0:        %{url}/archive/v%{version}/reach-%{version}.tar.gz

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
EOF
