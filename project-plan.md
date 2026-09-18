# TrinityXV6 — Project Plan (xv6 Max-Coverage Stack)

## MIT ideas included

| # | MIT idea | What we actually build | CO |
|---|---|---|---|
| 15 | Kernel threads + scheduler activations | 1:1 kernel threads `thread_create/exit/join/yield`, then user scheduler via upcalls (blocked / new-VP / preempted) | CO2, CO3 |
| 10 | Paging to disk + swapping | Swap out/in anonymous + page-cache pages to swap partition, clock/second-chance eviction, swap-slot allocator | CO4 |
| 11 | `mmap()` of files | `mmap/munmap/msync`, file-backed + anonymous, lazy fault fill, MAP_SHARED writeback | CO4, CO5 |
| 14 | Large pages (x86 2M/4M) | RISC-V Sv39 2 MB megapages (L1-leaf PTE) for kernel + opted-in user regions, promotion/demotion + TLB accounting | CO4 |
| 3 | Concurrent logging, EXT3-style | xv6 log with concurrent transactions: per-tx handles, fine-grained commit, group commit, no global log lock on op path | CO5 |
| 5 | FS snapshots + COW | Read-only snapshots of whole FS, COW on block + inode path, snapshot create/list/restore/delete | CO5 |
| 22 | RCU + lock-free name cache | Epoch RCU in kernel, dentry/name cache with lock-free reads, `namei` fast path | CO2, CO3 |

Priority if time runs short (cut from bottom): 14 first, then 22-lite (ship cache with locks, RCU as analysis), then 5 keeps single-snapshot only, then schedact multi-VP -> single-VP. Never cut 15-Phase-A, 10-basic, 3-basic — they are the grade core.

## Roles

- **H** = kernel / VM / scheduler / low-level. Owns `proc.c/h`, `swtch.S`, `trap.c`, `vm.c`, `swap.c`, `mmap.c`, VP/upcall kernel side, page eviction, megapage promotion.
- **S** = FS / syscalls / user lib / tests. Owns `log.c`, `fs.c`, `file.c`, `sysfile.c`, `sysproc.c` thread/mmap/snapshot syscalls, `rcu.c`, `namecache.c`, user `thread.c/h`, `mmap` + FS + RCU tests, benchmarks.
- **[H][S]** = joint design, integration, stress runs, report/demo. No feature merges without the other reviewing.

Rough balance: H heavier weeks 5–8 (schedact + VM), S heavier weeks 9–11 (log + snapshots + RCU). Weeks 1–4, 12–14 even by design.

---

## Week 1 — Bootcamp: xv6 under WSL + baseline numbers

### Study topics
- xv6 layout, `make qemu`, WSL quirks (no KVM — TCG only, slow; use `CPUS=1` daily, `CPUS=2` for SMP tests).
- Book ch. 1–7: syscalls, page tables, traps, locks, scheduling. `struct proc`, `scheduler()`, `yield()`, `usertrap()`.
- GDB + `make qemu-gdb`: break on `scheduler`, `usertrap`, `namei`, `commit`.

### Tasks
- [H] Toolchain + boot + `make grade` green on clean tree. Document QEMU flags, CPUS behavior.
- [S] GDB tracing guide (1 page): context switch, syscall entry, `namei` walk. Run uthread lab as warmup.
- [H][S] Record baselines: syscall cost, fork cost, FS write throughput, `usertests` time. These are your before/after numbers.
- [H][S] Git setup: `main` protected, per-feature branches + PRs, `known-good` tag every Friday.

### Done when
Both boot xv6, single-step a trap + a context switch, baselines in `bench/baseline.txt`.

---

## Week 2 — Design lock-in (gate for everything)

### Study topics
- Anderson et al. SOSP'91 scheduler activations (4 upcalls); Linux `clone`/pthread semantics (exit_group, join, TLS deferred).
- xv6 sleep/wakeup channels, spinlock discipline (`push_off/pop_off`), Sv39 PTE bits (V/R/W/X/U/A/D), log design (`log.c` phases).
- RCU epochs vs xv6 locks; snapshot COW options (block-bitmap + refcount vs redirect-on-write).

### Tasks
- [H] Design `struct thread` + `struct proc` delta: per-thread kstack/trapframe/context/state, run-queue integration, VP struct, swap/mmap PTE extensions (present/swapped/file-mapped bits), megapage promotion rule.
- [S] Design syscalls + semantics: `thread_create/exit/join/yield`, `mmap/munmap/msync`, `snapshot_create/list/restore/delete`, RCU API + name-cache invalidation rules. Decide: main-thread exit = process exit; `fork()` from multithreaded proc = disallowed in v1 (document); who allocates user stacks (user lib via `sbrk`/`mmap`, kernel validates).
- [H][S] Joint 3–5 page design doc + on-disk deltas (log header, snapshot superblock fields, swap-slot format). Sign off by both. No code without this.

### Done when
Design doc merged. Syscall numbers reserved in `syscall.h`. Test list for each MIT item stubbed in `tests/`.

---

## Week 3 — Threads data structures + `thread_create` (15-A)

### Study topics
- `allocproc/freeproc`, `kalloc/kfree`, `uvmalloc`, `copyin/out`, trapframe regs (a0/sp/ra), RISC-V syscall convention (a7 = nr).

### Tasks
- [H] `struct thread`: move kstack/trapframe/context to per-thread; `allocproc` creates main thread tid 1; `myproc()` via back-pointer; scheduler iterates threads not procs.
- [S] `thread_create` kernel side + syscall wiring: alloc thread + kstack + trapframe + user stack, copy parent regs, child a0 = 0, enqueue runnable. User wrapper `thread_create(fn, arg, stack)`.
- [H][S] Test: parent + child print distinct messages on separate kstacks (verify in GDB).

### Done when
Two threads share one address space concurrently.

---

## Week 4 — `thread_exit/join/yield` + scheduler + user lib (15-A done)

### Study topics
- `exit/wait` zombies, `sleep/wakeup` channels, `sched/yield`, `amoswap` user spinlock, wakeup races, double-free of kstacks.

### Tasks
- [H] Runnable-thread scheduler, `thread_yield`, sleeping thread blocks only itself (not whole proc).
- [S] `thread_exit` (free, wake joiners; main-thread exit tears down all threads) + `thread_join(tid)` + `thread.h/c` wrappers + user spinlock. Stress: N-thread counter, producer/consumer via yield, create/join churn under `CPUS=1` and `CPUS=2`.
- [H][S] Measure create/join/switch cost. **Fallback decision 1:** if threads unstable, stop here for threads and bank the time for VM/FS. Do not start schedact on a shaky base.

### Done when
Full thread API passes stress, costs recorded. Tag `phase-A-threads`.

---

## Week 5 — Scheduler activations: VP layer + preemption upcall (15-B single-VP)

### Study topics
- Anderson paper deep read (blocked / new-VP / preempted / notify); xv6 timer path `devintr -> yield`; `struct cpu`.

### Tasks
- [H] VP objects per proc (one kernel context = one VP), free-VP set, preemption upcall: on timer tick save user-thread state, build scheduler-handler stack, upcall into user scheduler.
- [S] Upcall delivery + skeleton user scheduler (run queue, dispatch, `schedctl_return` syscall to resume chosen thread).
- [H][S] Single-VP preemptive user-thread switching with zero kernel involvement per switch.

### Done when
User scheduler preempts + dispatches threads via upcalls on 1 VP.

---

## Week 6 — Schedact blocking + multi-VP (15-B done)

### Study topics
- Which xv6 syscalls block (pipe/console `read`, `sleep`, `wait`); upcall reentrancy (handler must not take user locks).

### Tasks
- [H] Blocked upcall at syscall-block boundary + new-VP-available when spare CPUs exist; VP allocation across CPUs.
- [S] User lib: off-queue blocked threads, dispatch next, `sched_yield`, VP pool mgmt, scheduler-aware (blocking, not spinning) mutex.
- [H][S] Tests: pipe reader/writer interleave, 2 VPs x 8 user threads, no lost wakeups. **Fallback decision 2:** if multi-VP racy, ship single-VP + kernel threads as complete threading story, move saved days to swap/log.

### Done when
Blocking I/O doesn't stall other threads; 2-VP parallel demo works. Tag `phase-B-schedact`.

---

## Week 7 — Swap: paging to disk (10 core)

### Study topics
- Sv39 fault path (`usertrap` store/page fault), `uvm*` helpers, clock/second-chance, swap-slot bitmap, dirty/accessed bits.

### Tasks
- [H] Swap area (dedicated file/partition image), slot allocator, victim selection (per-proc clock), swap-out (writeback dirty anon) + swap-in on fault, `sleep/wakeup` on in-flight pages to collapse duplicate faults.
- [S] `vmstat`-style counters (`pgfault, swapout, swapin, oom-kill`), test procs bigger than RAM, fork-with-swap policy (copy or mark-swapped), `sbrk` + swap interaction tests.
- [H][S] Correctness: random fault storm + `kill` during swap-in must not panic/leak.

### Done when
A proc 2x RAM runs to completion; counters prove out/in path; no leaks on kill. **Fallback decision 3:** if swap unstable, cap to anon-only, no file-backed swap yet (mmap week will extend it).

---

## Week 8 — `mmap` + megapages (11 + 14)

### Study topics
- `mmap` semantics (SHARED vs PRIVATE, `msync`, fault fill from `readi`), VMA list design, Sv39 L1-leaf megapage (2 MB, 512x4K), TLB flush (`sfence.vma`) scope.

### Tasks
- [H] VMA list per proc + fault handler branch: file-backed lazy fill, anon zero-fill, COW for PRIVATE (reuse snapshot COW helper if ready, else private copy). Megapage promotion for large aligned anon/VMA regions + demotion on partial unmap; `sfence.vma` correctness on both.
- [S] Syscalls `mmap/munmap/msync` + validation (overlap, prot, offset alignment), tests: shared counter via MAP_SHARED file, private-write isolation, `msync` persistence, 2 MB region reports megapage-mapped via debug counter.
- [H][S] Measure: fault latency 4K vs 2M, TLB-miss proxy (cycles/fault), mmap file read vs `read()` loop.

### Done when
MAP_SHARED/MAP_PRIVATE + msync pass; megapage counter + speedup recorded. Tag `phase-VM-done`.

---

## Week 9 — Concurrent logging (3 core)

### Study topics
- Current `log.c` (global lock, `begin_op/end_op`, `commit`), EXT3 group-commit + per-handle credits, deadlock avoidance (lock order log < inode < bitmap).

### Tasks
- [S] Per-transaction handles with buf credits, concurrent `begin_op` (no global lock on fast path), commit queue + group commit thread, absorbtion of duplicate block writes within a commit.
- [H] Review lock ordering + crash points; add `log_assert` invariants (outstanding creds, no dirty-outside-tx), `sleep/wakeup` for log-space waiters.
- [H][S] Tests: N-thread concurrent file creates/writes, `crash` fault-injection (power-cut after commit vs before — committed must survive via `make qemu` reboot check script).

### Done when
Concurrent writers scale vs stock log (report throughput), crash test passes. **Fallback decision 4:** if group-commit racy, ship concurrent-begin + serial-commit as v1.

---

## Week 10 — Snapshots + COW (5 core)

### Study topics
- Snapshot superblock, block refcounts, COW-on-write path (`writei` + `balloc` intercept), read-from-snapshot via versioned block pointer.

### Tasks
- [S] `snapshot_create/list/restore/delete` syscalls + on-disk format, COW: on write to snapshotted block, copy aside + bump refcount, snapshot reads pinned old copy. `restore` = mount snapshot as live (keep current as auto-snapshot).
- [H] PTE/mmap interplay: file-backed mmap writes to snapshotted file must COW too; OOM-on-snapshot-full policy (fail writes with ENOSPC, never panic).
- [H][S] Tests: write -> snapshot -> overwrite -> read both versions identical to pre/post; multi-snapshot chain; space accounting (`snap_blocks` counter).

### Done when
Create -> modify -> read-old + restore round-trip passes; space overhead = COW blocks only. Tag `phase-FS-done`.

---

## Week 11 — RCU name cache (22 core)

### Study topics
- Epoch RCU (grace period via context-switch/quiescentaz, `rcu_read_lock/unlock`, `call_rcu`), `namei` hot path, dentry lifetime vs `unlink/rename`.

### Tasks
- [S] `rcu.c` (epochs, grace detection on xv6 sched points) + fixed-size dentry cache (hash on parent+name -> inode nr + gen), lock-free `lookup`, locked insert/invalidate on create/unlink/rename.
- [H] Quiescent-state hooks in scheduler + trap return, `assert` on use-after-free (poison freed dentries in debug mode), review `sleep` inside read-side (forbidden — document).
- [H][S] Bench: `open/stat` loop 1-thread + N-thread, hit rate counter, correctness under concurrent rename/unlink storm.

### Done when
Name-cache hit rate + speedup recorded, storm test passes with no UAF. Lite fallback: ship cache with fine-grained locks + RCU analysis section if grace-period proves flaky — still a strong report.

---

## Week 12 — Hardening, stress, benchmarks (all tracks)

### Tasks
- [H] Race hunt `CPUS=2` extended: thread exit vs join vs proc exit, VP/upcall vs fork, swap-in vs kill, megapage promotion vs munmap, RCU grace vs `exec`. Fix + add kernel `assert`s (VP count, thread-list, swap-slot leak, log-credit, refcount sanity).
- [S] Regression suite one-command (`make stress-all`): threads, schedact pipe test, swap storm, mmap shared/private, concurrent log writers, snapshot round-trip, name-cache storm. Benchmark table: thread create/join, kernel-switch vs upcall-switch, pgfault/swapin latency, 4K vs 2M, log throughput 1-vs-N threads, namei hit/miss.
- [H][S] Edge triage: `exit()` from non-main thread, main exit with children, fork-from-thread denial message, swap-full + snapshot-full + log-full behavior, many-thread memory pressure. Freeze features end of week.

### Done when
`stress-all` green on `CPUS=1` and `CPUS=2` (3 consecutive runs), bench table filled with methodology notes.

---

## Week 13 — Integration + report draft

### Tasks
- [H] Unified demo script: boot -> thread/mmap/swap demo with `printf` traces -> snapshot demo -> `vmstat/logstat/namestat` outputs. Design-decision Q&A sheet (why 1:1 then schedact, why clock, why group-commit, why epochs).
- [S] Report draft: motivation, design, structs + on-disk formats, upcall/swap/mmap/COW/RCU protocols, measurements with graphs, lessons, "what next". Boot-log captures, memory-map + FS-format diagrams.
- [H][S] Doc freeze + full rehearsal on clean checkout. Cut list enforced: anything red becomes "future work" paragraph, not a late hack.

### Done when
Demo runs from clean clone in < 5 min; report draft complete minus final numbers.

---

## Week 14 — Demo, video, viva prep

### Tasks
- [H][S] 10-min live demo + 3-min video (boot -> parallel threads + blocking I/O -> big-proc swap -> mmap shared -> snapshot restore -> bench slide). Viva cards: one per MIT item (problem -> design -> tradeoff -> number).
- [H][S] Final polish: README (build/run/test/bench), tag `final`, short CHANGELOG from weekly tags.

### Done when
README + video + report delivered from `final` tag.

---

## Key files map

| File | Owner | What lives there |
|---|---|---|
| `proc.h/c`, `swtch.S` | H | threads, scheduler, VP structs, quiescent hooks |
| `trap.c`, `trampoline.S` | H | upcall delivery, fault dispatch (swap/mmap/megapage) |
| `vm.c`, `swap.c`, `mmap.c`, `vma.h` | H (S tests) | page tables, eviction, swap slots, VMA + promotion |
| `syscall.c/h`, `sysproc.c` | S (H reviews) | thread/mmap/snapshot/rcu-stat syscall dispatch |
| `log.c`, `fs.c/h`, `file.c`, `sysfile.c` | S | concurrent log, COW write path, snapshot ops |
| `snap.c/h`, `rcu.c/h`, `namecache.c/h` | S | snapshots, epochs, dentry cache |
| `user/thread.c/h`, `user/mmaptest.c`, `user/fstest.c`, `user/rcutest.c` | S | user sched, all stress/bench programs |

## Risks & fallbacks

- **Schedact doesn't fit.** Decide end of Week 6: ship kernel threads + single-VP user scheduler, move days to swap/log. Still a strong project.
- **Swap/mmap destabilizes kernel.** Cap to anon-only swap + MAP_PRIVATE first; SHARED + megapages are week-8 stretch inside the week.
- **Log/snapshot scope creep.** Lock on-disk formats end of Week 2; no format change after Week 9 without both agreeing + bumping version.
- **RCU grace bugs.** Lite fallback pre-agreed: locked cache + RCU design analysis still earns marks; don't burn week 12 on it.
- **WSL/TCG slowness.** `CPUS=1` daily, `CPUS=2` only for SMP gates; report relative speedups, not absolute ns.
- **Merge conflicts.** H never touches `log.c/fs.c` internals, S never touches `vm.c/proc.c` internals without PR review. `known-good` tag every Friday, branch per feature.

## Deliverables

1. Design doc (week 2) + weekly tags.
2. Thread + schedact demo (weeks 4, 6).
3. VM demo: big-proc swap + mmap + megapage counters (weeks 7–8).
4. FS demo: concurrent-log throughput + snapshot round-trip (weeks 9–10).
5. RCU name-cache bench (week 11).
6. `stress-all` + bench table (week 12), report + video (weeks 13–14).

---

## What else you can add (ranked, pick at most 2)

1. **Capabilities (#6, cheap, +security CO).** fd-bound caps (`can_read/can_write/can_snapshot`), 3–4 days on top of S's FS syscalls. Best value-add if faculty wants security. Do first.
2. **`fsck` + crash-injection harness (cheap, +viva).** Offline checker for log + snapshot refcounts + swap-slot leaks; scripted QEMU-kill mid-commit. 2–3 days, huge defensibility. Do second.
3. **Per-thread `ps` + `vmstat/logstat/namestat` shell cmds (cheap, +demo).** Visibility wins vivas. 1–2 days.
4. **Loadable FS module (#12, medium).** Only if you want "extensibility" story — read-only DOS FAT as module on top of week 10. 1 week, needs module loader + syscall trampolines.
5. **Software RAID-5 (#8, medium).** Only instead of #12, not as well — needs multi-disk QEMU setup, good if your course weights storage perf/fault-tolerance. 1 week.
6. **TLS + futex-ish blocking + priority donation (medium, +threads depth).** Natural follow-on to 15 if schedact finishes early. 4–5 days.
7. **Do NOT add:** VMM (#1), TXT (#2), DSM (#7), migration (#9), 64-bit (#18), port (#19), window system (#20), TSX-in-QEMU (#23) — each is a second project, needs HW/cluster, and adds zero max-coverage.

> Suggested final scope if ahead in week 12: add (1) caps + (2) fsck/crash harness. If behind: cut 14, keep everything else.
