# TrinityXV6 design doc — Week 2 (gate for everything)

Status: S sections COMPLETE (authoritative for FS/syscall/user-lib/RCU work). H sections
are stubs marked PENDING — they get filled by H review before week-3 code starts. This
satisfies "no code without design sign-off": S work is specified end to end; H work is
scoped but not authored here.

Sign-off: S = done below; H = pending; joint = after H fills its sections.

Sections owned by each role (per project-plan roles table):

- S owns: §2 (syscall semantics), §3.1/3.2 (snapshot + name-cache on-disk/data formats
  shared with H review), §4 (user-lib), §5 (test matrix), §7 (formats: log header,
  snapshot superblock, swap-slot layout spec).
- H owns: §1 (thread/proc/VP/PTE deltas), §6 (lock ordering review, crash points,
  swap/mmap PTE encoding).
- Joint: §8 (open questions).

---

## 1. Kernel data-structure design — H OWNED, PENDING

Placeholder for H: `struct thread` vs `struct proc` delta (per-thread
kstack/trapframe/context/state, run-queue integration), VP struct for scheduler
activations, swap/mmap PTE extensions (present / swapped / file-mapped bits), megapage
promotion rule. S consumes only these interfaces from H:

- `mythread()` semantics (S code must work when called from any thread of a proc)
- Which thread state the kernel reports to `sysproc` (tid table for `ps`, week 12)
- PTE flag bits S must not collide with in file-backed VMA handling (see §3.3)

Do not proceed to week-3 S kernel code until H fills this section.

## 2. Syscall semantics — S owned

### 2.1 Numbering

Reserved in `kernel/syscall.h` (23–34). No semantics change without a new doc rev.
Numbers are frozen end of week 2 per the plan's format-lock rule.

### 2.2 Threads (MIT #15-A)

| Syscall | Signature (user) | Kernel behavior |
|---|---|---|
| `thread_create` 23 | `int thread_create(void (*fn)(void*), void *arg, void *stack)` | Allocate thread in current proc: kstack, trapframe, context (H provides alloc; S provides syscall + stack validation). Set child `epc = fn`, `sp = stack + guard-adjusted top`, `a0 = arg`. Enqueue runnable. Parent gets child tid; error returns -1. |
| `thread_exit` 24 | `[[noreturn]] void thread_exit(int status)` | Current (non-main) thread: free its kstack/trapframe, wake joiners, never returns. Main thread: full process teardown (see decision D1). |
| `thread_join` 25 | `int thread_join(int tid, int *status)` | Block on channel keyed to (proc, tid) until target exits; copy out status; tid may not be reused while a joiner waits (gen counter in tid). Joining self → -1; unknown tid → -1. |
| `thread_yield` 26 | `void thread_yield(void)` | Current thread → RUNNABLE, reschedule. Whole proc keeps running other threads. |

Semantics decisions locked here:

- **D1 — main-thread exit = process exit.** `exit()` from the main thread (tid 1) tears
  down all sibling threads first (they are killed at safe points, joiners woken with
  error -2), then runs the normal xv6 proc exit. Rationale: matches POSIX
  `exit_group`; simplest correct teardown; S implements the kill-and-wake walk in
  `sysproc`/`proc` teardown path jointly reviewed by H.
- **D2 — `fork()` from a multithreaded proc is disallowed in v1.** Return -1 with
  errno-style marker `E_MTFORK` (kernel sets `p->tf->a0 = -1`); documented in
  `user/thread.h` and printed once by the user lib. Revisit never (v1 lock).
- **D3 — user stacks are allocated by the user lib, validated by the kernel.**
  `thread_create` callers pass a stack from `sbrk`/`malloc` (user lib helper
  `thread_stack_alloc` hands out page-aligned chunks from one sbrk arena). Kernel
  validates: stack range mapped, page-aligned, within proc's user range, writable,
  and non-overlapping with any other live thread stack (S keeps a per-proc
  stack-range table; freed on thread exit). Kernel never allocates user memory for
  stacks in v1.

### 2.3 mmap (MIT #11)

| Syscall | Signature (user) | Kernel behavior |
|---|---|---|
| `mmap` 27 | `void *mmap(int fd, uint64 off, uint64 len, int prot, int flags)` | Create VMA: file-backed (fd) or anonymous (fd = -1). Lazy fill on fault. `MAP_SHARED` writes go to page cache + writeback via inode; `MAP_PRIVATE` COW. Returns user VA or 0 on error. |
| `munmap` 28 | `int munmap(void *addr, uint64 len)` | Unmap full pages in range; `MAP_SHARED` dirty pages writeback first; demote megapages if partial. |
| `msync` 29 | `int msync(void *addr, uint64 len)` | Flush dirty file-backed pages in range through the log (one transaction); blocks until commit. |

S-owned constraints: length/offset page-aligned (offset) or rounded up (len); prot must
be R/W combo; fd must be a regular file for file-backed; overlap with existing VMA →
error. VMA list design, fault fill, PTE bits = H (§1); S owns the syscall validation +
the `msync`→`writei`/log path.

### 2.4 Snapshots (MIT #5)

| Syscall | Signature (user) | Kernel behavior |
|---|---|---|
| `snapshot_create` 30 | `int snapshot_create(void)` | Freeze current FS state as read-only snapshot (COW from here on). Returns snap id ≥ 1. |
| `snapshot_list` 31 | `int snapshot_list(struct snapinfo *out, int max)` | Fill up to `max` entries {id, created_tick, blocks}; returns count. |
| `snapshot_restore` 32 | `int snapshot_restore(int id)` | Mount snapshot as live FS; current live state auto-saved as a fresh snapshot first (see plan). All fds other than console/pipe are invalidated (return -1 on use). |
| `snapshot_delete` 33 | `int snapshot_delete(int id)` | Drop snapshot; COW refcounts allow reclaim when no other snapshot pins the block. |

Rules: create/delete are log transactions (crash-safe); `restore` = create + swap roots
+ reopen (also transactional); max 8 live snapshots (param.h `NSNAP`); ENOSPC when COW
can't allocate — writes fail with -1, never panic (plan risk rule).

### 2.5 RCU name cache (MIT #22)

Kernel-internal API, plus one visibility syscall:

| Syscall | Signature | Kernel behavior |
|---|---|---|
| `namestat` 34 | `int namestat(struct namestat *out)` | Copy out {lookups, hits, misses, invalidations, grace_periods} for bench/regression. |

RCU API S implements in `rcu.c`: `rcu_read_lock/unlock` (preemption-disabling
read-side critical sections, xv6-appropriate), `rcu_free(cb, ptr)` deferred free,
grace detection at scheduler + trap-return quiescent points (H provides hooks, §6).
Cache API in `namecache.c`: `nc_lookup(parent, name) -> inode*`, `nc_insert`,
`nc_invalidate(parent, name)` — invalidation is synchronous + locked at create/unlink/
rename time; lookups are lock-free reads inside `rcu_read_lock`. `sleep` inside
read-side is forbidden (documented in rcu.h when written).

### 2.6 Threads/schedact upcalls (MIT #15-B, weeks 5–6)

`upcall` delivery syscall shape (`schedctl_return`, `vp_yield`) is reserved but NOT
numbered in week 2 — H co-designs the delivery stack in §1 first. Numbers 35–38 are
earmarked for the upcall path; do not use for anything else.

## 3. Data structures — S owned, H reviewed

### 3.1 Per-proc stack table (D3)

Fixed array in proc ext (S adds alongside H's thread table, same file placement H
chooses): `{uint64 lo, hi; int tid;}` × `NTHREADS` (16, param.h). Populated at
`thread_create` (stack + one guard page below), removed at thread exit. Lookup is
O(n) once per create — hot path unaffected.

### 3.2 Dentry cache entry (`namecache.c`)

Fixed table `NDENTRIES` = 512 (param.h), hash on (parent inum, name hash, gen):
`{parent, namehash, gen, dev, inum, state, lastuse}`. Insert/lookup/invalidate rules
per §2.5. Generation counters make stale entries detectable after inode reuse.

### 3.3 VMA interplay (S constraints on H design)

File-backed dirty-page writeback must go through `writei` (log-transactional) so
snapshots (§2.4) and msync (§2.3) share one commit path. H's PTE "file-mapped" bit
must not be confused with swapped pages: S relies on exactly three states per page —
resident / swapped / unmapped — with file-backed fill handled in H's fault path via
`readi`.

## 4. User library — S owned

`user/thread.h` / `thread.c`:

- `thread_create(fn, arg, stack)` — thin wrapper over syscall 23.
- `thread_stack_alloc(nbytes)` / `thread_stack_free(stack)` — arena over sbrk,
  page-aligned, one guard page between stacks (guard page is simply left unmapped by
  never returning it to callers).
- `thread_mutex_t` — user spinlock (amoswap.d.aq / amoswap.rl), used by stress tests
  until week-6 scheduler-aware mutex.
- `thread_exit(status)` wrapper marks arena stack free + syscall.

`user/mmap.h`, `user/snap.h` — signature mirrors of §2.3/§2.4.

## 5. Test matrix stubs — S owned (files created empty under `tests/`)

| MIT # | Item | Stub file | Grows into (weeks 3–12) |
|---|---|---|---|
| 15-A | kernel threads | `tests/threads/` | smoke (2 threads print on own kstacks), N-counter, producer/consumer, create/join churn |
| 15-B | schedact | `tests/schedact/` | pipe interleave, 2 VP × 8 threads, lost-wakeup storm |
| 10 | swap | `tests/swap/` | proc > RAM, fault storm, kill-during-swap-in |
| 11 | mmap | `tests/mmap/` | shared counter, private isolation, msync persistence, 2M megapage counter |
| 14 | megapages | `tests/mmap/` (same dir) | 4K vs 2M fault latency, promotion/demotion |
| 3 | concurrent log | `tests/log/` | N-writer create/write, crash-reboot check script |
| 5 | snapshots | `tests/snap/` | write→snap→overwrite→read both, chain, restore round-trip |
| 22 | RCU name cache | `tests/rcu/` | open/stat loop, hit-rate, rename/unlink storm |

Each stub dir carries a `README` listing the eventual test programs + pass criteria so
week-3+ work only fills files in. Runner integration (`make stress-all`) is a week-12
item; per-item user programs land in `user/` as they are written.

## 6. Lock ordering / crash points / PTE encoding — H OWNED, PENDING

Placeholder for H: global lock order (log < inode < bitmap < ...), log crash-point
invariants + `log_assert` list, swap/mmap PTE bit encoding, upcall stack layout,
RCU quiescent-hook placement. S commitments H needs: §3.3 states, §2.4 ENOSPC rule,
`nc_*` call sites all run with inode locks held or inside rcu read-side (never both
held across sleep).

## 7. On-disk / ABI deltas — S authored, H reviews, frozen end of week 2

### 7.1 Log header (concurrent log, MIT #3)

Disk block 2 (unchanged position). Today: `{n, block[n]}` with a single global tx.
Week-9 delta (format frozen now):

```
struct loghdr {
  uint n;              // blocks in the current committed-but-uninstalled set
  uint block[MAXOPBLK];// destination block numbers
  uint txseq;          // monotonic tx sequence number of the installed set
  uint epoch;          // format version = 1
};
```

- Only ONE uninstalled set at a time (same as xv6 recovery) — concurrency happens
  before commit (group commit accumulates), not during install. Recovery protocol is
  therefore UNCHANGED: replay if `txseq` on disk > installed seq in superblock.
- Per-tx handles live only in RAM (credits, outstanding count) — never on disk.
- MAXOPBLK stays 30 (log.c) unless group commit needs batching; any bump must change
  FS block 2 sizing + `epoch`.

### 7.2 Snapshot superblock fields (MIT #5)

Superblock (block 1) gains a tail — offsets count from `sb.size`-aligned end, all
fields uint unless noted:

```
snap_root      64 × uint    // per-slot: block addr of snapshot bcache root dir; 0 = empty
snap_seq       uint         // highest allocated snap id ever (ids never reused)
snap_flags     uint         // bit0: COW enabled
snap_epoch     uint         // format version = 1 (bump = incompatible)
```

Live 8 slots × 4 B = 32 B + 12 B header fields. Everything else about snapshot roots
(block-tree versioning, refcount table location) is week-10 detail; the frozen part is:
field names, order, sizes, epoch semantics, and "snapshot data blocks are addressed by
the same 64-bit block numbers as live FS".

### 7.3 Swap-slot format (MIT #10)

Swap lives on a dedicated raw partition (QEMU second virtio disk, added week 7):
sectors hold page-sized (4 KB) slots, 8 sectors/page = 1 slot.

```
slot bitmap:        RAM-resident (rebuilt at boot by scanning swap signature)
per-slot layout:    4096 B page image, no metadata inline
swap signature:     first sector of swap partition starts with magic "TRXSWP1"
                    followed by {nslots, pagesz} — rebuild + sanity check at boot
```

Swapped pages carry NO per-page disk metadata; identity lives in the PTE (H encodes
slot # in PTE bits, §6). Format freeze: 4 KB slots, 8-slot page mapping, magic string,
RAM-resident bitmap. No change after week 7 without joint sign-off + epoch bump.

### 7.4 Syscall ABI

Numbers per §2.1 (frozen). Arg passing: ≤ 4 scalar args in a0–a3 via trapframe (xv6
`argint/argaddr/argstr` style, extended to 4); pointer args validated with
copyin/copyout only. struct layouts passed through pointers (`snapinfo`, `namestat`)
live in `kernel/stat.h`-adjacent headers and are mirrored in `user/` — mirror drift is
a PR-review checklist item.

## 8. Open questions — JOINT

- Q1 (H): thread table placement inside `struct proc` vs separate array — affects S's
  stack-table co-location (§3.1). Needed before week-3 code.
- Q2 (H): does `myproc()` stay proc-returning with `mythread()` added, or flip? S code
  uses whichever H picks; syscall layer is unaffected.
- Q3 (joint): group-commit batching size for week 9 — measure first (baseline exists).
- Q4 (joint): `restore` semantics for open mmap VMAs on snapshotted files — proposal:
  munmap-all file-backed VMAs on restore (simple); revisit if tests disagree.
