#!/usr/bin/env python3
"""Interleaved timing harness for mathicgb variants.

Runs every (binary, input) cell once per round, in round-robin order, so that
machine drift is spread across all cells rather than concentrated in one.
Reports min / median / mean / stdev of user CPU time and wall time.
"""
import argparse, os, resource, statistics, subprocess, sys, time

def run_once(binary, args, cwd):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.monotonic()
    r = subprocess.run([binary] + args, cwd=cwd,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wall = time.monotonic() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if r.returncode != 0:
        sys.exit(f"FAILED: {binary} {' '.join(args)} -> {r.returncode}")
    return (after.ru_utime - before.ru_utime, wall)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--reps", type=int, default=7)
    p.add_argument("--cwd", required=True)
    p.add_argument("--input", action="append", required=True,
                   help="project name, optionally NAME:extra,args")
    p.add_argument("--bin", action="append", required=True,
                   help="LABEL=/path/to/mgb")
    p.add_argument("--reducer", default="26")
    p.add_argument("--extra", default="")
    a = p.parse_args()

    bins = [b.split("=", 1) for b in a.bin]
    cells = {}
    for label, _ in bins:
        for inp in a.input:
            cells[(label, inp)] = []

    for rep in range(a.reps):
        for label, path in bins:
            for inp in a.input:
                name, _, extra = inp.partition(":")
                argv = [ "gb", name, "-reducer", a.reducer ]
                argv += a.extra.split() + ([e for e in extra.split(",") if e])
                cells[(label, inp)].append(run_once(path, argv, a.cwd))
                print(f"  rep {rep+1} {label:12s} {inp:24s} "
                      f"user={cells[(label,inp)][-1][0]:8.3f} "
                      f"wall={cells[(label,inp)][-1][1]:8.3f}", flush=True)

    def stats(v):
        return (min(v), statistics.median(v), statistics.mean(v),
                statistics.stdev(v) if len(v) > 1 else 0.0)

    for inp in a.input:
        print(f"\n== {inp}  (reducer {a.reducer}, {a.reps} reps)")
        print(f"{'variant':14s} {'min':>8s} {'med':>8s} {'mean':>8s} "
              f"{'sd':>7s} {'sd%':>6s} {'vs base':>8s}   (user CPU s)")
        base = None
        for label, _ in bins:
            u = [c[0] for c in cells[(label, inp)]]
            mn, md, mean, sd = stats(u)
            if base is None:
                base = md
            rel = f"{100*(md/base - 1):+6.1f}%" if base else ""
            print(f"{label:14s} {mn:8.3f} {md:8.3f} {mean:8.3f} {sd:7.3f} "
                  f"{100*sd/mean if mean else 0:5.1f}% {rel:>8s}")
        print(f"{'':14s} {'wall min':>8s} {'med':>8s}")
        for label, _ in bins:
            w = [c[1] for c in cells[(label, inp)]]
            print(f"{label:14s} {min(w):8.3f} {statistics.median(w):8.3f}")

main()
