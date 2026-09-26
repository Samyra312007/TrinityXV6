# TrinityXV6 design doc — Week 2 (gate for everything)

Status: COMPLETE (rev 2, week-2 gate). S sections were authored first and are unchanged
except §2.1/§2.6 (35–38 assignment, noted in both); H sections (§1, §6) are now
authored, and H has reviewed the S sections with sign-off notes inline (§3.1, §3.2,
§7). This satisfies "no code without design sign-off": both tracks are specified end
to end and jointly signed off. Week-3 kernel code may start against §1/§6.

Sign-off: S = done below; H = done below; joint = done (H reviewed §2/§3/§7; S-visible
constraints re-checked against §1/§6 in §6.6). Formats per §7 and syscall numbers per
§2.1 are FROZEN per the plan's format-lock rule.

Sections owned by each role (per project-plan roles table):

- S owns: §2 (syscall semantics), §3.1/3.2 (per-proc stack table + dentry cache,
  H-reviewed), §4 (user-lib), §5 (test matrix), §7 (formats: log header, snapshot
  superblock, swap-slot layout spec, H-reviewed).
- H owns: §1 (thread/proc/VP/PTE deltas), §6 (lock ordering, crash points, swap/mmap
  PTE encoding, upcall stack layout, RCU hooks).
- Joint: §8 (open questions — all resolved or explicitly deferred, see §8).

---

## 1. Kernel data-structure design — H OWNED, COMPLETE

Authored by H in the week-2 gate window; resolves Q1 and Q2 (§8). Design only — week-3
code implements exactly this. S consumes these interfaces from H:

- `mythread()` semantics (§1.2): S code works when called from any thread of a proc
- Thread state reported to `sysproc` (§1.4: `thread_snapshot` helper + `procinfo`)
- PTE bits S must not collide with (§1.6: `PTE_S`/`PTE_M`, bits 8–9, RSW)

### 1.1 Thread table placement (resolves Q1)

The thread table lives INSIDE `struct proc` as a fixed array — not a separate global
table. Rationale:

- One lock guards it: the proc's existing spinlock also guards per-thread state, so
  thread transitions add no new lock to the ordering (§6.1) and no new channels.
- Teardown (decision D1) is a single walk of one proc's array — no cross-object
  references to chase while killing siblings.
- S's §3.1 stack-range table co-locates as a parallel per-proc array (same struct,
  same lock), satisfying "same file placement H chooses".
- Size check: NPROC (64) × NTHREADS (16) × ~100 B ≈ 100 KB of BSS — trivial.

`NTHREADS = 16` is added to `param.h` in week 3 (S's §3.1 already assumes it).

### 1.2 struct thread / struct proc delta

Threads reuse `enum procstate` (UNUSED, USED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE);
ZOMBIE means "exited, status pending, joinable" for a thread. Proc-level state
collapses to {UNUSED, USED, ZOMBIE}: the proc is USED while any thread lives;
SLEEPING/RUNNABLE/RUNNING are thread-level facts only.

```c
struct thread {
  int tid;                     // 1-based within proc; 0 = slot free
  uint gen;                    // bumped on slot reuse; join token is (tid, gen)
  enum procstate state;
  struct proc *proc;           // back-pointer; resolves via cpu->thread
  uint64 kstack;               // kalloc'd page at fixed VA (below)
  struct trapframe *trapframe; // page in the reserved trapframe window (below)
  struct context context;
  void *chan;                  // sleep channel (moved out of struct proc)
  int xstate;                  // exit status, read by thread_join while ZOMBIE
};

struct proc {
  struct spinlock lock;        // now guards ALL threads of the proc
  enum procstate state;        // UNUSED / USED / ZOMBIE only
  int killed;                  // proc-wide (kill() semantics unchanged in v1)
  int xstate;                  // proc exit status (main-thread exit per D1)
  int pid;
  struct proc *parent;
  uint64 sz;
  pagetable_t pagetable;
  struct file *ofile[NOFILE];
  struct inode *cwd;
  char name[16];
  struct thread threads[NTHREADS];   // threads[0] = main thread, tid 1
  struct { uint64 lo, hi; int tid; } ustacks[NTHREADS];  // S §3.1 table
};
```

Fields removed from `struct proc`: `chan` (moves to thread; `sleep_prepare`/`sleep`/
`wakeup` become thread-level), `kstack`, `trapframe`, `context` (all move to thread).

- `struct cpu` gains `struct thread *thread;` alongside `proc`; `c->proc` and
  `c->thread` are set and cleared together in `scheduler()` and at every thread
  switch, always with interrupts off (existing `push_off` discipline).
- Kernel stacks: static VA reservation, lazy PA mapping. `TKSTACK(pi, ti)` is a fixed
  VA per (proc slot, thread slot) carved next to today's `KSTACK(i)` window; kalloc at
  thread create + `kvmmap` into the kernel pagetable, unmap + kfree at exit. Fixed VAs
  mean a stale TLB entry can only ever alias the same slot, and every map/unmap pairs
  with `sfence_vma()` — no dynamic kernel-VA allocator needed.
- Trapframes: one page per thread in a reserved user-address-space window
  `[TRAPFRAME − (NTHREADS−1)·PGSIZE, TRAPFRAME]`, slot i at `TRAPFRAME − i·PGSIZE`.
  `uservec`/`userret` touch only the running thread's slot (the kernel passes the
  current thread's trapframe pointer where today it passes `p->trapframe`).
  Consequence: the user-size ceiling in `growproc` moves from `TRAPFRAME` to
  `TRAPFRAME − NTHREADS·PGSIZE` (H implements; stated so §3.3 address assumptions
  hold). `exec` maps only the slots it creates, not all 16.
- `allocproc` creates the proc with main thread tid 1 in `threads[0]`; `freeproc`
  frees every thread slot (kstacks, trapframes). Reclamation order makes join-vs-exit
  races (week-4 tests) impossible by construction: a thread is reaped only under
  p->lock, by join or teardown, never both.
- `myproc()` stays proc-returning (resolves Q2): the codebase's proc-keyed state
  (locks held, cwd, ofile, log transactions) is per-PROC identity; flipping would
  churn every S call site for zero benefit. `mythread()` is added alongside
  (`c->thread`); S's syscall layer uses it only where thread identity matters (tid,
  per-thread state, join tokens). Both resolve through the same `c->proc`/`c->thread`
  pair, so S code calling `mythread()` is correct from any thread of the proc.

### 1.3 Scheduler integration

- `scheduler()` keeps xv6's global scan: walk `proc[]`; for each USED proc, walk
  `threads[]`; run the lowest-index RUNNABLE thread. Fairness = round-robin over
  (proc, thread) pairs per pass — unchanged stock xv6 semantics, no per-cPU queues.
- `sched()` invariants keep their shape: `holding(&p->lock)` and
  `mycpu()->noff == 1` stay; the state checks move to `mythread()->state`.
- `yield()` becomes thread-level: the CALLING THREAD goes RUNNABLE and reschedules;
  sibling threads of the proc compete for the hart normally.
- `sleep_prepare`/`sleep`/`wakeup` operate on threads (`chan` lives in struct thread);
  `wakeup(chan)` walks procs × threads (64 × 16 worst case — same order as today's
  proc scan). A channel hash is a week-12 stretch, not v1.
- `kill(pid)` stays proc-wide in v1: sets `p->killed`, wakes all sleeping threads of
  the proc; the trap-return path checks the proc flag. Per-thread kill is out of scope.

### 1.4 sysproc reporting contract (tid table for `ps`, week 12)

One copyout-able struct when `ps` lands (no syscall number consumed in week 3):

```
struct procinfo {
  int pid; char name[16];
  int nthreads;
  int tids[NTHREADS]; uint8 tstates[NTHREADS];  // enum procstate per live thread
};
```

H provides `thread_snapshot(struct proc *p, struct procinfo *out)` (fills under
p->lock) in week 3 alongside the syscall wiring; S's `sysproc`/`ps` consumes only this
helper, never the raw arrays.

### 1.5 VP layer (MIT #15-B, weeks 5–6 — shape frozen, code gated on phase-A)

- One kernel context = one VP: a VP is a live thread slot acting as a virtual
  processor for user-level threads. Per-proc VP objects `{int id; int state;
  struct thread *kthread;}` with states {VP_FREE, VP_RUNNING, VP_IN_UPCALL}; the
  free-VP set is a per-proc bitmap over thread slots, guarded by p->lock.
- Upcalls (blocked / new-VP / preempted) are delivered on a dedicated per-proc upcall
  kstack (1 page at proc create); the user scheduler entry point is registered per
  proc via the schedctl syscalls (§2.6, layout in §6.4). VP transitions happen only
  under p->lock.
- VP code lands weeks 5–6 per plan, gated on fallback decisions 1 and 2. This section
  freezes only the objects and their lock ownership so week-3 structs need no rework.

### 1.6 Swap/mmap PTE extensions (S-visible contract for §3.3)

Sv39 PTE bits 8–9 (RSW) carry the two new software bits; hardware ignores RSW, so
plain pages are unaffected:

```
#define PTE_S (1L << 8)   // RSW1: page is swapped out (meaningful with V=0)
#define PTE_M (1L << 9)   // RSW2: page is file-mapped resident (V=1; msync path)
```

The three S-visible states of §3.3 map exactly:

| State | PTE encoding | Fault behavior |
|---|---|---|
| resident anon | V=1, M=0 | none |
| resident file-mapped | V=1, M=1 | none; writeback walks M |
| swapped | V=0, S=1; PPN field (bits 53:10) = swap slot id | fault → swap-in (§6.3) |
| unmapped / VMA fill | V=0, S=0 | fault → demand-zero or file fill via H's VMA path |

Slot capacity: 28 bits of PPN field → 2^28 slots × 4 KB = 1 TB addressable, far beyond
the 128 MB-RAM test scale; the frozen §7.3 on-disk format is unaffected. Normative S
rule: S code never sets/clears bits 8–9 directly — it goes through vm.c helpers (week
7/8: `swap_out`/`swap_in`, file-map fill, `pte2level()`). This is the "bits S must not
collide with" commitment.

### 1.7 Megapage promotion rule (MIT #14)

- Format: Sv39 L1-leaf (level-1 PTE with R/W/X) = 2 MB page, 512 × 4 KB.
- Promotion trigger: anon or file-backed VMA region ≥ 2 MB, VA 2 MB-aligned, with ≥ 3/4
  of its 512 L0 slots resident (per-VMA counter maintained at fault-fill — O(1)
  check, no scans). Promotion rewrites the L1 entry as one 2 MB leaf, frees the L0
  table, bumps `n_megapages`.
- Demotion triggers: munmap/msync touching any sub-range of the megapage, COW break on
  a MAP_PRIVATE megapage, or swap-out of any resident page within it. Demotion copies
  the touched page into a fresh 4 KB leaf set and re-fills the rest lazily.
- TLB: v1 always issues full `sfence_vma()` on promotion/demotion/unmap (correctness
  first, matching the kernel-page discipline); per-address `sfence.vma rs1` is a
  week-8 stretch goal, not a format decision.
- Kernel megapages: `kvmmake()` maps aligned kernel text/data regions with L1 leaves
  statically; no runtime kernel promotion.
- Accounting: per-proc counters `n_megapages`, `promotions`, `demotions` — the debug
  counter for tests/mmap (§5) and the week-8 bench.

## 2. Syscall semantics — S owned

### 2.1 Numbering

Reserved in `kernel/syscall.h` (23–34). No semantics change without a new doc rev.
Numbers are frozen end of week 2 per the plan's format-lock rule.

Rev 2 addition (H, co-signed): 35–38 are assigned per §2.6/§6.4 (schedctl/vp upcall
path); they land in `kernel/syscall.h` at week-5 wiring, not before. 23–34 remain
exactly as frozen in rev 1.

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

Numbers 35–38 are now ASSIGNED by H (doc rev 2, delivery stack in §6.4) per §2.1's
rule:

| Syscall | Number | Signature (user) | Purpose |
|---|---|---|---|
| `schedctl_register` | 35 | `int schedctl_register(void (*handler)(struct upcall*))` | Register the per-proc user scheduler entry point. |
| `schedctl_return` | 36 | `void schedctl_return(uint64 token)` | Resume the user thread identified by token after an upcall. |
| `vp_yield` | 37 | `void vp_yield(void)` | Give the VP back to the kernel. |
| `vp_alloc` | 38 | `int vp_alloc(void)` | Acknowledge/claim a new VP offered by the kernel. |

Full semantics are co-designed by H and S in weeks 5–6. Do not use 35–38 for anything
else.

## 3. Data structures — S owned, H reviewed

### 3.1 Per-proc stack table (D3)

Fixed array in proc ext (S adds alongside H's thread table, same file placement H
chooses): `{uint64 lo, hi; int tid;}` × `NTHREADS` (16, param.h). Populated at
`thread_create` (stack + one guard page below), removed at thread exit. Lookup is
O(n) once per create — hot path unaffected.

H sign-off (rev 2): placement resolved by §1.1 — the table lives in `struct proc` as
`ustacks[NTHREADS]`, guarded by p->lock (no separate lock; see §6.1). Validation
pairs the `tid` column with §1.2's (tid, gen) join tokens.

### 3.2 Dentry cache entry (`namecache.c`)

Fixed table `NDENTRIES` = 512 (param.h), hash on (parent inum, name hash, gen):
`{parent, namehash, gen, dev, inum, state, lastuse}`. Insert/lookup/invalidate rules
per §2.5. Generation counters make stale entries detectable after inode reuse.

H sign-off (rev 2): layout approved. `lastuse` is maintained by insert/invalidate
only — lock-free lookups must not write it (§6.5 read-side is read-only); hit-rate
statistics come from the `namestat` counters (§2.5).

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

## 6. Lock ordering / crash points / PTE encoding — H OWNED, COMPLETE

Authored by H in the week-2 gate window; consumes S's commitments (§3.3 states, §2.4
ENOSPC rule, `nc_*` call-site rules) and returns the encodings and hooks S needs.

### 6.1 Global lock order

Acquire in this order; never reverse. Sleeplocks (inode, buf) are a separate axis: a
spinlock may be acquired while holding a sleeplock, never the reverse; sleeping while
holding a spinlock stays forbidden as today.

```
pid_lock            <  proc->lock      (allocpid runs under proc->lock)
wait_lock           <  proc->lock      (kwait/kexit order, unchanged)
proc->lock          -> leaf            (threads, chan, ustacks §3.1, VP set §1.5)
log.lock            -> leaf            (never taken while holding proc->lock)
bcache locks        -> leaf            (eviction lock + per-buf sleeplock)
swap.lock (wk 7)     -> leaf            (slot bitmap; released before any I/O sleep)
rcu (wk 11)          -> no lock        (read-side = preemption off, §6.5)
```

New rules this design adds:

- proc->lock guards ALL per-thread state (tid, gen, thread state, chan, xstate) and
  S's stack-range table — code holding p->lock already holds the thread lock; no
  second acquisition, no new deadlock edge.
- log.lock is never acquired while holding proc->lock: log-touching paths (iput on
  cwd teardown, writei) run with at most the inode sleeplock held, as in stock xv6.
  The week-9 concurrent-log redesign (S) must keep log.lock strictly leaf: per-tx
  handles take log-space under log.lock and block via sleep/wakeup channels, never by
  holding inode locks across the wait.
- `wakeup()` may be called with any spinlock held except the proc->lock of a proc
  whose threads are being woken (unchanged xv6 property; wakeup takes each proc->lock
  transiently).
- Sleeplock axis: inode < buf (ilock held across bread is legal; bread never takes
  inode locks). S's `nc_*` call-site rule (inode locks held OR inside rcu read-side,
  never both held across sleep) is permitted by this axis plus §6.5.

### 6.2 Log crash points + log_assert list (feeds S's week-9 work)

Crash windows under the frozen §7.1 protocol (one uninstalled set; replay if on-disk
txseq > superblock seq):

1. During write_log: header still n=0 → the crash discards the tx. Safe.
2. After write_head, before install: header n>0, txseq newer → recovery replays. Safe.
3. During install_trans: replay is idempotent (same blocks rewritten). Safe.
4. After install, before header zeroing: replay rewrites identical data. Safe.

The only unsafe act is a header write naming blocks whose data is not yet in the log
area: `write_head()` happens strictly after `write_log()`, ordered by the bwrite
chain. Week-9 group commit must preserve exactly this property — batch into the log
area BEFORE the header names it.

`log_assert` invariants (always-on kernel asserts):

- log_write: `log.outstanding >= 1`; `log.lh.n < LOGBLOCKS`; dedupe keeps lh.n ≤
  distinct blocks touched by live txs.
- end_op: `outstanding >= 0`; `committing == 0` while any tx is open.
- commit: every lh.block entry's buf is bpin'd; after install_trans every pin returns
  to 0; `lh.n == 0` before commit returns.
- begin_op waiters: after a commit completes, no thread remains asleep on `&log`
  while space is available (wakeup must cover both wait reasons: committing, full).
- Global: when `outstanding == 0 && lh.n == 0`, no buf is bpin'd by the log (the
  no-dirty-outside-tx check).

### 6.3 PTE encoding (normative — mirrors §1.6)

`PTE_S (1L<<8)` = swapped, `PTE_M (1L<<9)` = file-mapped. When V=0 && S=1 the PPN
field carries identity: `slot = (pte >> 10) & 0xFFFFFFF` (28 bits). Fault dispatch in
usertrap for store/load page faults: V=0 && S=1 → swap-in, then retry; V=0 && S=0 →
VMA fill (demand-zero anon or `readi` per VMA); V=1 && write fault && !W → COW (week
8). A/D bits: week 7 sets MENVCFG_ADUE at boot if the QEMU model honors SvuAD so
eviction reads hardware A bits; the fallback is clear-W marking — recorded in week 7,
not frozen here.

### 6.4 Upcall stack layout (weeks 5–6; shape frozen so week-3 structs don't move)

- Per-proc upcall kstack: 1 kernel page allocated at proc create; holds the upcall
  frame `{reason, vp_id, thread_token, saved_trapframe_ptr}` built by the kernel.
- Preempted upcall: user-mode timer → usertrap saves the running user thread's regs
  into its trapframe slot, switches sp to the upcall kstack, sets user epc to the
  registered scheduler entry (schedctl_register, 35), userret. The handler may not
  take user locks (reentrancy rule: S's user scheduler uses only amoswap ops and
  lock-free queues).
- Blocked upcall: at a syscall sleep boundary with runnable user threads and a free
  VP, the kernel delivers new-VP (38) on another hart instead of sleeping the whole
  proc; the blocked thread is parked off-queue by S's user lib.
- Resume: schedctl_return (36) re-enters the chosen user thread; vp_yield (37) gives
  the VP back. Numbers 35–38 assigned here per §2.1's rule — recorded as doc rev 2.

### 6.5 RCU quiescent hooks (week 11)

- `rcu_read_lock`/`unlock` = `push_off`/`pop_off` only (preemption off; no lock
  acquired) — cannot invert §6.1. Sleep inside read-side is forbidden and ASSERTED:
  `sleep_prepare` panics in debug builds when the caller's rcu_depth > 0.
- Quiescent-state hooks (lock-free, atomic counters only):
  (a) `scheduler()` — bump this hart's counter each time it picks a thread;
  (b) `prepare_return()` — bump before returning to user mode (covers harts parked in
  user space between traps).
- Grace completion frees on the observing hart: `call_rcu` work drains in the
  scheduler's idle path (the wfi loop) once the grace epoch passes.
- Debug: deferred-freed dentries are poisoned (0xDD fill) before the deferred kfree
  so use-after-free faults deterministically (plan's week-11 debug requirement).

### 6.6 S commitments honored (cross-check)

- §3.3 three page states — implemented exactly by the §1.6 encoding.
- §2.4 ENOSPC rule — snapshot COW allocation failures return -1 from writei paths;
  no panic; balloc failure inside a tx unwinds via the existing end_op path.
- `nc_*` call sites — permitted by the §6.1 sleeplock axis plus §6.5 read-side rules.

## 7. On-disk / ABI deltas — S authored, H reviews, frozen end of week 2

H review (rev 2): COMPLETE. 7.1 — recovery protocol unchanged; the §6.2 crash-point
list matches it one-for-one. 7.2 — the snapshot tail (64 × 4 B roots + 12 B fields =
276 B) fits block 1's free space (superblock uses 8 uints of 1024 B; log region
touched only via logstart, no clash). 7.3 — RAM-resident slot bitmap + PTE-carried
page identity (§1.6/§6.3 slot field) are mutually consistent; the fault path needs no
per-page disk metadata. Formats FROZEN as written.

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

## 8. Open questions — JOINT (resolved, rev 2)

- Q1 (H) — RESOLVED: thread table lives inside `struct proc` (§1.1); S's stack table
  co-locates there (§3.1 sign-off). No separate global table.
- Q2 (H) — RESOLVED: `myproc()` stays proc-returning; `mythread()` is added alongside
  (§1.2). S syscall code uses `mythread()` only where thread identity matters.
- Q3 (joint) — DEFERRED BY DESIGN, per the original note: week-9 group-commit batch
  size is measured against `bench/baseline.txt` before sizing; the frozen §7.1 format
  already accommodates batched commit (one uninstalled set).
- Q4 (joint) — RESOLVED: adopt the proposal — `snapshot_restore` munmaps all
  file-backed VMAs (dirty pages flushed via the §2.3 msync path first); anonymous VMAs
  are untouched. Simple and correct; revisit if week-10 tests disagree.
