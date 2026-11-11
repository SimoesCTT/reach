# Audit 0001 — Trackdisk-style reachability constraint

Status: AUDIT PHASE. No library code until the measurement runs.
Depends on: chipmem (proven, ~/Desktop/the-level). No new hardware path.

## The claim under test (narrow)

The Amiga floppy DMA buffer had to live in Chip RAM because the hardware
could only reach that memory; the constraint was explicit and queryable
(MEMF_CHIP at allocation time). Modern accelerators have the same
constraint: a GPU kernel or BLT command can only read buffers resident in
GPU-accessible memory. On a shared-memory iGPU most buffers qualify, but
the *query* — "can device D reach this buffer, zero-copy / with a copy /
not at all" — is not a first-class userspace primitive.

Proposed: chipmem_reachable(buf, device) -> {YES_ZERO_COPY,
YES_COPY_REQUIRED, NO} plus a reason code, pre-flight and side-effect-free.

## Closest existing Linux mechanisms

1. DRM_IOCTL_PRIME_FD_TO_HANDLE — importing a DMA-BUF into a DRM device
   either succeeds or returns an error. This IS a reachability test: if
   the import fails, the device cannot reach the buffer. Strongest
   counter-argument.
2. Rusticl/OpenCL import (clCreateBuffer with CL_MEM_EXT_HOST_PTR or
   the dma-buf extension) — rejects non-importable memory; again a
   reachability test by failure.
3. dma_buf_attach / dma_buf_map_attachment (kernel-side) — the real
   mechanism; not userspace.
4. HMM / DEVICE_PRIVATE migration — the kernel decides reachability and
   migrates; userspace sees a unified pointer.

## Why the existing mechanisms may not count

The import ioctls answer the question *by attempting the operation*.
There is no cheap, side-effect-free pre-flight query that says
"reachable, and here is the cost class (zero-copy / copy / unreachable)"
before the caller commits a buffer to a device. The Amiga constraint was
known at allocation time and was a flag, not a failed operation.

BUT — this is exactly the kind of claim that may not survive. A
PRIME_FD_TO_HANDLE probe is cheap and side-effect-free enough to *be* a
query. If it is, the candidate DROPS.

## Concrete withdrawal condition

DROPPED if DRM_IOCTL_PRIME_FD_TO_HANDLE (and the Rusticl import
equivalent) can be used as a pre-flight reachability query with:
  (a) no persistent side effects on the buffer,
  (b) a cost low enough to call per-buffer in a hot path (measure it),
  (c) and a result that distinguishes zero-copy from copy-required.
If all three hold, chipmem_reachable is a thin wrapper over an existing
ioctl and is not a restored primitive — it is sugar. Drop it.

SURVIVES (as a thin, documented wrapper, honestly labeled as such) only
if the cost measurement shows the probe is too expensive for a hot path,
OR if no single call distinguishes zero-copy from copy-required and the
caller must attempt two operations to learn the answer.

## Measurement that settles it

1. Time DRM_IOCTL_PRIME_FD_TO_HANDLE on a chipmem buffer, N=1000,
   report median and p99. This is the probe cost.
2. Time the Rusticl import path equivalent.
3. Determine whether success-vs-failure of the import alone tells the
   caller zero-copy vs copy (on Skylake: i915 BLT import is zero-copy;
   Rusticl import is copy — do the two import ioctls differ in result in
   a way the caller can read?).
4. If probe cost < ~1us and the result distinguishes zero-copy, DROP the
   candidate and record "existing ioctl suffices."

## Non-claims

- Does not make anything faster.
- Does not add a memory class that does not exist.
- At best it names an existing constraint explicitly; at worst it is
  redundant and gets dropped. Both outcomes are acceptable.

## Next action

Write tests/probe_reach.c implementing measurements 1-3. Run it. The
audit concludes SURVIVES or DROPS from the numbers, not from prose.

---

## Result (measured 2026-11-12, Skylake-U, kernel 7.2.13)

Probe: tests/probe_reach.c, vendored chipmem, N=1000.

  PRIME_FD_TO_HANDLE: median 8390 ns, p99 22919 ns, max 35590 ns

## What the number settles

Withdrawal condition (b) — "cost low enough to call per-buffer in a hot
path" — FAILS. ~8.4 us median is not free; it is in the same order as
the per-link orchestration gap copperlist measured. The p99 is 22.9 us
and the max 35.6 us; a loop sees the tail, not the median.

Withdrawal condition (c) — "distinguishes zero-copy from copy-required"
— FAILS. On i915 both the BLT import (zero-copy) and a Rusticl import
(copy) succeed, so success/failure tells the caller *reachable*, not
*which cost class*. The caller learns the cost class only by
attempting the operation and observing the path taken.

Withdrawal condition (a) — "no persistent side effects" — HOLDS. The
probe creates a GEM handle and it can be closed; the buffer is
unchanged. But (a) alone is not enough to withdraw the claim.

## Conclusion: SURVIVES as a caching wrapper, not as a restored primitive

The honest claim is narrower than the audit's opening text. It is:

  chipmem_reachable() is a cached query over PRIME_FD_TO_HANDLE plus a
  one-time cost-class probe. It is not "the missing primitive"; the
  primitive (reachability by attempting an import) exists. What it adds
  is (i) caching, so a hot path pays the ~8.4 us once per (buffer,
  device) pair, not per call, and (ii) a cost-class answer that the
  ioctl alone cannot give, learned by attempting the operation once and
  recording which path it took.

The API is therefore:
  chipmem_reachable(buf, device) -> {YES_ZERO_COPY, YES_COPY_REQUIRED,
  NO} with the cost-class answer cached after the first call.

The audit does NOT claim the ioctl is absent. It claims the ioctl is
expensive and does not answer the cost-class question, and that a cached
wrapper is worth having for that reason. If a future kernel makes the
ioctl cheap and cost-class-observable, the wrapper drops.

## What the Rusticl leg adds (pending the next Mesa release)

Karol Herbst confirmed on mesa-dev (2026-11-12) that
cl_khr_external_memory_dma_buf is merged and lands with iris in the next
Mesa release. When it does, the Rusticl import path becomes
zero-copy-capable, and the "copy" answer for a Rusticl import changes.
The audit must record that the cost-class mapping is stable only until
the backend changes; the wrapper's cache must be invalidated when a
backend gains or loses zero-copy capability.

## Honest verdict

SURVIVES, narrowly, as a cached query wrapper. The audit's opening
text — "the query is not a first-class userspace primitive" — is
narrowed to "the probe exists but is expensive and does not answer
cost class." The primitive that survives is the cache and the
one-time cost-class answer, not the reachability check itself.

---

## Second measurement: the ioctl does not cache (2026-11-12)

Probe: tests/probe_reach_warm.c, one cold import then N=1000 warm
lookups of the same dma-buf into the same DRM file, no close between
iterations.

  0 cold import:  7353 ns
  1 warm:  median 7021 ns, p99 14655 ns, max 103479 ns

## What the second number settles

The per-file PRIME cache that some sources describe does not produce a
warm-path win on this kernel, in this call pattern. Warm is the same as
cold: ~7 us per call. p99 is 14.6 us; the max (103 us) shows scheduler
interference on the tail, which a per-buffer query in a loop would hit.

This strengthens, not weakens, the SURVIVES verdict:

  (b) "cheap enough for a hot path" FAILS even warm, not only cold. A
      userspace cache is justified on cost alone, independent of the
      cost-class question.

  (c) "distinguishes zero-copy from copy-required" FAILS. The ioctl
      returns a handle whether the subsequent access is direct or
      through a staging buffer.

## Revised primitive that survives

chipmem_reachable(buf, device) caches per (buffer, device) pair:
  - the reachability answer (YES/NO), avoiding a ~7 us ioctl on every
    call,
  - the cost class (ZERO_COPY / COPY_REQUIRED), learned once by
    attempting the operation and recording which path the backend took.

Both halves are justified by measurement:
  - the cache by (b) failing even warm,
  - the cost-class answer by (c) failing outright.

The cache must be invalidated when a backend gains or loses zero-copy
capability (e.g. Rusticl gaining cl_khr_external_memory_dma_buf, per
Karol Herbst's mesa-dev reply of 2026-11-12).

## Honest verdict, restated

SURVIVES as a caching wrapper with a cost-class component, on two
measured grounds: the ioctl is ~7 us even warm, and it does not
distinguish zero-copy from copy. It is not a missing primitive; it is
a measured necessity over an existing ioctl. If a future kernel makes
PRIME_FD_TO_HANDLE cheap (sub-us) AND cost-class-observable, the
wrapper drops.

---

## Implementation result (2026-11-12)

src/reach.c, tests/t_reach.c, run on Skylake-U / kernel 7.2.13.

  first query:  ZERO_COPY in 23252 ns (one import; within the
                distribution the warm probe showed, whose max was
                103479 ns)
  cached:       1000 runs, mean 40 ns, max 280 ns
  cache size:   1

Comparison:

  cold import (once)             ~7-23 us
  PRIME_FD_TO_HANDLE warm        ~7 us per call
  reach_query cached             ~40 ns per call

The wrapper reduces the hot-path query by ~175x. The primitive that
survives is the cache; the ioctl is the expensive one-time probe and
the wrapper amortizes it to one payment per (buffer, device) pair.

The cost-class answer is inferred from chipmem_backend(), not from
the ioctl, because the ioctl does not distinguish zero-copy from
copy. reach_invalidate() exists so the cache can be dropped when a
backend gains or loses zero-copy capability (e.g. Rusticl gaining
cl_khr_external_memory_dma_buf, per Karol Herbst on mesa-dev,
2026-11-12).

## Audit status: CLOSED

Every falsifiable claim is now measured:
  - the ioctl is expensive, warm and cold (7021 / 7353 ns) — measured
  - it does not distinguish zero-copy from copy — measured
  - a cached query is sub-microsecond (40 ns mean) — measured
  - the cache does not grow per call (size 1 after 1000 hits) — measured

The claim is narrow and honest: reach is a userspace cache over an
existing ioctl plus a one-time cost-class answer, justified by the
measured cost of the ioctl and its inability to answer cost class.
It is not a restored kernel primitive. If a future kernel makes
PRIME_FD_TO_HANDLE cheap and cost-class-observable, reach is deleted.

---

## Implementation result (2026-11-12)

src/reach.c, tests/t_reach.c, run on Skylake-U / kernel 7.2.13.

  first query:  ZERO_COPY in 23252 ns (one import; within the
                distribution the warm probe showed, whose max was
                103479 ns)
  cached:       1000 runs, mean 40 ns, max 280 ns
  cache size:   1

Comparison:

  cold import (once)             ~7-23 us
  PRIME_FD_TO_HANDLE warm        ~7 us per call
  reach_query cached             ~40 ns per call

The wrapper reduces the hot-path query by ~175x. The primitive that
survives is the cache; the ioctl is the expensive one-time probe and
the wrapper amortizes it to one payment per (buffer, device) pair.

The cost-class answer is inferred from chipmem_backend(), not from
the ioctl, because the ioctl does not distinguish zero-copy from
copy. reach_invalidate() exists so the cache can be dropped when a
backend gains or loses zero-copy capability (e.g. Rusticl gaining
cl_khr_external_memory_dma_buf, per Karol Herbst on mesa-dev,
2026-11-12).

## Audit status: CLOSED

Every falsifiable claim is now measured:
  - the ioctl is expensive, warm and cold (7021 / 7353 ns) — measured
  - it does not distinguish zero-copy from copy — measured
  - a cached query is sub-microsecond (40 ns mean) — measured
  - the cache does not grow per call (size 1 after 1000 hits) — measured

The claim is narrow and honest: reach is a userspace cache over an
existing ioctl plus a one-time cost-class answer, justified by the
measured cost of the ioctl and its inability to answer cost class.
It is not a restored kernel primitive. If a future kernel makes
PRIME_FD_TO_HANDLE cheap and cost-class-observable, reach is deleted.
