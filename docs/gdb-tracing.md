# GDB tracing guide — context switch, syscall entry, namei walk

One window runs QEMU stopped at boot; the other is GDB. Port = `uid % 5000 + 25000`
(`make print-gdbport` prints it; `.gdbinit` is generated from the template automatically).

```bash
make qemu-gdb CPUS=2                                    # window 1
tools/bin/riscv64-unknown-elf-gdb kernel/kernel         # window 2
(gdb) set confirm off
(gdb) c
```

SMP note: with `CPUS=2` each hart shows up as a GDB thread. For single-stepping
use `CPUS=1` so you never chase a switch to another hart mid-sequence; for SMP
races use `info threads` + `thread N`, or set `xo`-style breakpoints on all
harts: `break swtch` fires on whichever hart gets there.

## 1. Context switch (`swtch` + `scheduler`)

Goal: watch a full switch — callee-saved regs saved into `struct context`,
then restored into another thread's context, with `ra` doing the magic jump.

```
(gdb) break swtch
(gdb) c
(gdb) p *old            # ra/sp of the context being saved (mostly garbage until filled)
(gdb) p *new            # ra of the target: usually scheduler or usertrapret path
(gdb) x/14gx old        # raw context struct
(gdb) stepi 20          # single-step through swtch.S: sd/ld pairs
(gdb) x/i $pc           # ret -> jumps through new->ra
(gdb) p $ra             # now equals new->ra captured above
(gdb) finish
```

Breakpoints worth adding around it:

- `scheduler` — fires every time a hart picks a new proc; `p p->name`,
  `p p->state` to see who won the run queue.
- `yield` — where a running proc voluntarily gives up the CPU (timer tick:
  `usertrap` → `yield`).
- `sleep` — pass a channel: `p (char*)chan` and match it against the
  `wakeup` side to connect producer/consumer.

## 2. Syscall entry (user → `syscall()` → back)

Goal: see the number in `a7`, args in `a0..a5`, and the return value written
back to `a0` — the whole RISC-V syscall convention in one stop.

```
(gdb) break syscall
(gdb) c
(gdb) p p->trapframe->a7        # syscall number (see kernel/syscall.h)
(gdb) p/x $sp                   # kernel sp; arg regs live in the trapframe
(gdb) p p->trapframe->a0        # arg0 (and the return reg on the way back)
(gdb) finish                    # run the handler
(gdb) p p->trapframe->a0        # return value delivered to user
(gdb) break usertrapret         # the way back out
```

Trap plumbing between the two: `uservec` (trampoline.S) stashes user regs in
`p->trapframe`, `usertrap` dispatches, `usertrapret` + `userret` restore.
Break on `usertrap` and check `p p->trapframe->cause` — 8 = ecall-from-U.

## 3. `namei` walk (path → inode)

Goal: watch the directory tree being walked one dirent at a time.

```
(gdb) break namei
(gdb) c
(gdb) x/s name                  # the path being resolved
(gdb) p *namei(...) is not what you want here; instead:
(gdb) break namex
(gdb) c
(gdb) p dp->dev                 # device of the directory inode
(gdb) p dp->inum                # directory inode number
(gdb) break namecmp
(gdb) c
(gdb) x/s s                     # each dirent name compared during the scan
(gdb) finish                    # 0 == match -> descend into that entry
```

Per component xv6: `namex` locks the dir inode, `readi`s it in dirent-sized
chunks, `namecmp`s each against the current path component, then recurses via
`dirlookup` → `iget`. `skipelem` (fs.c) splits off the next component — break
there once to see `"a/b/c"` become `"a"`, then `"b"`, then `"c"`.

## Handy extras

```
(gdb) layout split                  # source + asm (Ctrl-X 1/2 to cycle)
(gdb) x/i $pc                       # current instruction
(gdb) p $sstatus                    # hart privilege state
(gdb) p/x sfence_vma                # n/a: search vm.c for sfence instead
(gdb) watch p->state                # catch every state transition of a proc
```

Week-1 checklist (from project-plan.md): context switch single-stepped,
syscall entry traced end-to-end, `namei` walk of `/README` traced from root
dirent to final inode.
