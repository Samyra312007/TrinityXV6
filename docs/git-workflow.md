# Git workflow (weeks 1–14)

## Branch model

- `main` is protected: no direct pushes, everything lands via PR.
- One branch per feature, named by owner-track + item:

```
feat/h-<item>     H-track features   (e.g. feat/h-thread-struct)
feat/s-<item>     S-track features   (e.g. feat/s-concurrent-log)
feat/hs-<item>    joint features     (e.g. feat/hs-schedact)
fix/<area>        bugfixes           (e.g. fix/fs-refcount)
doc/<name>        docs only          (e.g. doc/week2-design)
```

- Branch off current `main`, rebase before opening the PR, delete after merge.

## PR rules

- **No merge without the other track reviewing** — the plan's [H][S] rule:
  H never merges changes to `log.c`/`fs.c` internals, S never merges changes
  to `vm.c`/`proc.c` internals, without the other as reviewer.
- CI (run locally before every push):

```bash
source tools/env.sh
make clean && make -j$(nproc) && make fs.img
./test-xv6.py -q usertests
```

- PR description: what, why, how tested, any on-disk/syscall ABI impact
  (formats are frozen after week 2 — bumping one needs both owners).

## Commit style

- Imperative mood, area prefix: `fs:`, `vm:`, `sched:`, `log:`, `bench:`,
  `doc:`. e.g. `log: add per-transaction handle credits`.
- One logical change per commit; never commit generated artifacts
  (`*.o`, `kernel/kernel`, `fs.img`, `mkfs/mkfs`, `user/_*`).

## Tags

- `known-good` every Friday — annotated, dated:

```bash
git tag -a known-good-$(date +%Y%m%d) -m "week N: usertests green CPUS=1+2"
git push origin known-good-$(date +%Y%m%d)
```

- Phase tags when plan milestones land: `phase-A-threads` (wk 4),
  `phase-B-schedact` (wk 6), `phase-VM-done` (wk 8), `phase-FS-done` (wk 10).

## Baseline discipline

- `bench/baseline.txt` is append-only history: add a dated section per
  measurement session, never overwrite old numbers — the report needs the
  before/after story.
- Numbers always recorded with the same config: CPUS=1 primary, CPUS=2
  secondary, same host, same toolchain commit.
