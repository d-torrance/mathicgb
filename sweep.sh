#!/bin/bash
# Thread-count sweep of mgb's F4 reducer on examples/yang1.ideal.
#
# Submit from the top of a mathicgb checkout, with a TBB-enabled mgb on
# PATH (or MGB set to it):
#
#   sbatch -A <account> -q inferno -p <partition> sweep.sh
#
# Each job writes runs/<jobid>/ holding machine.txt, ldd.txt and times.csv.
# Three rounds over the thread counts, rather than three runs of each in a
# row, so that drift on the node is spread across every count.

#SBATCH -J mgb-sweep
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH -t 8:00:00
#SBATCH -o %x-%j.out

set -u

MGB=${MGB:-$(command -v mgb)}
TOP=${SLURM_SUBMIT_DIR:-$PWD}
OUT=$TOP/runs/${SLURM_JOB_ID:-local}
mkdir -p "$OUT"
cp "$TOP/examples/yang1.ideal" "$OUT/"
cd "$OUT"

{
  hostname
  nproc
  lscpu
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
} > machine.txt
ldd "$MGB" > ldd.txt

N=$(nproc)
echo "rep,threads,wall,user,sys" > times.csv
for rep in 1 2 3; do
  for T in 1 2 4 8 16 32 64 96 128 192; do
    [ "$T" -le "$N" ] || continue
    /usr/bin/time -a -o times.csv -f "$rep,$T,%e,%U,%S" \
      "$MGB" gb yang1 -reducer 26 -threadCount "$T" > /dev/null 2>&1
  done
done
