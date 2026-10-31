#!/bin/sh
# The cache lines one audio callback touches, in the Blackfin simulator (needs the bfin-elf-sim image: make sim):
#   [TRACE_PRESET=n] [TRACE_DRIVE=0..32767] tools/cachetrace.sh [plugin dir, from the repository root]
# Builds the plugin's sources as the bench does (-DOP1_HOST), linked in plugin.json's order after tools/bench/trace.c
# (three held notes, one block per voice per callback), traces every instruction fetch and data access
# (--trace-core) from the first traced callback on, and hands the trace to tools/cachetrace.py.
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)
plugdir=${1:-op1r-src/synth_sdk/plugins/moai}
srcs=$(python3 -c "import json,sys; print(' '.join(json.load(open(sys.argv[1]))['sources']))" "$root/$plugdir/plugin.json")
out="$root/build/trace"; mkdir -p "$out"
CF="-O2 -mcpu=bf524 -ffreestanding -fno-builtin -nostdlib -fno-common -DOP1_HOST -I/r/op1r-src/synth_sdk -I/r/tools/bench -I/r/tools/bench/freestanding"
docker run --rm -v "$root":/r bfin-elf-sim sh -ec "
  mkdir -p /tmp/t && cd /tmp/t
  objs=''
  for s in $srcs; do bfin-elf-gcc $CF -c /r/$plugdir/\$s -o \$s.o; objs=\"\$objs \$s.o\"; done
  bfin-elf-ld -r \$objs -o engine.o
  bfin-elf-gcc $CF -DENGINE=ENGINE_moai -DTRACE_PRESET=${TRACE_PRESET:-0} ${TRACE_DRIVE:+-DTRACE_DRIVE=$TRACE_DRIVE} -c /r/tools/bench/trace.c -o trace.o
  bfin-elf-as -mcpu=bf524 /r/tools/bench/start.S -o start.o
  bfin-elf-ld -e __start start.o trace.o engine.o \$(bfin-elf-gcc -print-libgcc-file-name) -o trace.elf
  bfin-elf-nm -n -S trace.elf > /r/build/trace/nm.txt
  mark=\$(bfin-elf-nm trace.elf | awk '\$3 == \"_trace_mark\" { print \"0x\" \$1 \":\" }')
  bfin-elf-run --trace-core --trace-file /dev/stdout trace.elf | awk -v mark=\"\$mark\" '
    \$3 == \"-IBUS\" && \$8 == mark { on = 1; print \"M\"; next }
    !on { next }
    \$3 == \"-IBUS\" { print \"I\", substr(\$8, 1, length(\$8) - 1); next }
    \$3 == \"-DBUS\" && (\$4 == \"LOAD\" || \$4 == \"STORE\") { print substr(\$4, 1, 1), substr(\$8, 1, length(\$8) - 1), \$5 }' > /r/build/trace/trace.txt"
python3 "$here/cachetrace.py" "$out/nm.txt" "$out/trace.txt"
