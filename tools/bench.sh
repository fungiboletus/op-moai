#!/bin/sh
# Blackfin cycle counts in the simulator (needs the bfin-elf-sim image: make sim): tools/bench.sh [plugin dir in the SDK]
#   - the engine per scenario, and supersaw for reference (tools/bench/bench.c: what the numbers mean)
#   - the engine's parts (tools/bench/parts.c)
#   - the same scenarios built for the host: their output hashes must equal the Blackfin ones (the inline assembly
#     and its C twins agree bit for bit)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)
plugdir=${1:-op1r-src/synth_sdk/plugins/moai}
rel=${plugdir#op1r-src/synth_sdk/}
CF="-O2 -mcpu=bf524 -ffreestanding -fno-builtin -nostdlib -fno-common -DOP1_HOST -I/r/op1r-src/synth_sdk -I/r/tools/bench -I/r/tools/bench/freestanding"
sim() {   # $1 label, $2 engine symbol, $3 engine source (relative to the SDK)
    docker run --rm -v "$root":/r bfin-elf-sim sh -ec "
      mkdir -p /tmp/b && cd /tmp/b
      bfin-elf-gcc $CF ${PROF:-} -c /r/op1r-src/synth_sdk/$3 -o engine.o
      bfin-elf-gcc $CF ${PROF:-} -DENGINE=$2 -c /r/tools/bench/bench.c -o bench.o
      bfin-elf-as -mcpu=bf524 /r/tools/bench/start.S -o start.o
      bfin-elf-ld -e __start start.o bench.o engine.o \$(bfin-elf-gcc -print-libgcc-file-name) -o bench.elf
      bfin-elf-run bench.elf" | sed "s/^/$1  /"
}
PROF=-DMM_PROF sim "$(basename "$plugdir") (stages)" ENGINE_moai "$rel/moai.c" | grep "per sample" || true
out=$(sim "$(basename "$plugdir")" ENGINE_moai "$rel/moai.c")
echo "$out"
sim supersaw ENGINE_supersaw plugins/supersaw/supersaw.c
docker run --rm -v "$root":/r bfin-elf-sim sh -ec "
  mkdir -p /tmp/p && cd /tmp/p
  bfin-elf-gcc $CF -I/r/$plugdir -c /r/tools/bench/parts.c -o parts.o
  bfin-elf-as -mcpu=bf524 /r/tools/bench/start.S -o start.o
  bfin-elf-ld -e __start start.o parts.o \$(bfin-elf-gcc -print-libgcc-file-name) -o parts.elf
  bfin-elf-run parts.elf" | sed "s/^/parts  /"
mkdir -p "$root/build"
cc -O1 -DOP1_HOST -DENGINE=ENGINE_moai -I"$root/op1r-src/synth_sdk" -I"$here/bench" "$here/bench/bench.c" \
   "$root/$plugdir/moai.c" -o "$root/build/bench_host" 2>/dev/null
"$root/build/bench_host" | sed 's/.*hash /hash /' > "$root/build/hash_host.txt"
echo "$out" | sed 's/.*hash /hash /' > "$root/build/hash_bfin.txt"
if cmp -s "$root/build/hash_host.txt" "$root/build/hash_bfin.txt"; then echo "host == Blackfin output: identical"
else echo "host != Blackfin output:"; paste "$root/build/hash_host.txt" "$root/build/hash_bfin.txt"; exit 1; fi
