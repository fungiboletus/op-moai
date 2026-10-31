# Making moai cheaper on the OP-1

v1 sounded right but was too heavy: one voice with an FX, a master FX and the tape glitched, and poly / unison was out
of reach. This note records how the cost was measured, where it went, what v2 changed, and what is left to try.

The versions below (v1 .. v3.26) are the engine's history before it was moai: it was called minimoog (engine names
`minimoog`, `minimoog2`, `minimoog3` on the OP-1).

## Measuring without an OP-1

`make sim` builds gdb's Blackfin instruction-set simulator (`bfin-elf-run`) on top of the SDK's cross compiler;
`make bench` (tools/bench.sh) then:

- compiles the engine with the plugin build's flags (`-O2 -mcpu=bf524`) and renders 32 blocks per scenario, reading
  the Blackfin `CYCLES` counter around each `render()` call (tools/bench/bench.c);
- does the same for **supersaw** as a reference (an engine of the same SDK that is fine on the OP-1);
- times the parts (one oscillator pass, one filter sample, the per-block helpers: tools/bench/parts.c);
- builds the same scenarios for the host and compares output hashes: the inline assembly and its C twins must give
  **bit-identical** audio (they do).

The simulator's timing model was checked with a micro-benchmark: a 16 x 16 multiply costs 1 cycle, a 32 x 32 multiply
3, a library division about 185, loads and taken branches have their stalls. It does **not** model the OP-1's memory:
plugin files are loaded into SDRAM and run through the caches (the loader flushes / invalidates them after
relocating), so on the device a render also pays cache misses, mostly for the first voice in each audio callback.
The numbers below are a floor, good for comparing versions and engines, not the OP-1's absolute load.

`make cachetrace` (tools/cachetrace.sh) traces every instruction fetch and data access of a few audio callbacks with
three held notes (tools/bench/trace.c; `TRACE_PRESET`, `TRACE_DRIVE` pick the sound) and counts the 32-byte lines one
callback touches, per function and per variable, and how they fall into the BF524's caches (tools/cachetrace.py). It
cannot see the firmware's own code and data, which share those caches.

## Where v1's cycles went

| v1, cycles per sample per voice | |
|---|---|
| defaults (2 saws, no sub) | 299 |
| saw + sub | 354 |
| pulse + sub | 453 |
| triangle mix + sub | 493 |
| supersaw (reference) | 83 |

- Every product was written as `int32 * int32`. GCC sees that the values fit in 16 bits after the clamps, drops the
  16-bit casts and emits the 3-cycle 32 x 32 multiply (`R0 *= R1`) instead of the 1-cycle 16 x 16 one.
- One loop did everything per sample (3 oscillators, mix, ladder, saturators): about 30 live values for 8 data
  registers, so the compiler shuffled them through pointer / index registers and the stack every sample.
- Seven library divisions per block (polyBLEP reciprocals, the ladder's feedback solve, the SDK's voice index which
  divides by 0x24): about 1300 cycles per block, 10 per sample.
- The ladder kept a 32-bit remainder per stage (error feedback against the dead zone at low cutoffs): seven
  operations and a register per stage.

## What v2 changed (no change in sound: levels within 0.3 dB, same filter response and self-oscillation)

| change | effect |
|---|---|
| passes: each oscillator adds into the output buffer, then the ladder runs in place | each loop keeps its state in registers |
| 16 x 16 multiplies in inline assembly (`MUL16`, `FM_HL`, ...) with C twins for the host | 1 cycle each, and the compiler cannot widen them |
| ladder states in Q16: `v = hi(d) * gg` (fractional) is already Q16 | a stage is subtract, multiply, add, add; no remainder needed (residual error < 1 LSB) |
| the ladder's state sum as one MAC chain on the accumulator | 4 instructions, no temporaries |
| the saturator's limit at 2^29 in Q16 | clamped by a saturating shift (no constant registers) |
| reciprocal tables for the feedback solve and the polyBLEP; the voice index by a multiply; the key's reciprocal and octave cached per key | no division in the audio path |
| pitch as a linear ramp per block | no multiply per sample for pitch, intervals or detune |
| one compare (unlikely) for "near an oscillator edge" | the common path does not branch |

| v2 (`minimoog2`), cycles per sample per voice | |
|---|---|
| CLASSIC (2 saws, osc 3 off) | 100 |
| CLASSIC + osc 3 | 118 |
| MINOR chord + osc 3 | 119 |
| BREATH (2 saws + noise) | 112 |
| SHARK + osc 3 (saw + triangle mixes) | 147 |
| NASAL + osc 3 (three pulses: the most expensive) | 169 |
| supersaw (reference) | 83 |

About 3x fewer cycles than v1 at the same settings; the default setup costs about 1.2 supersaws. Roughly: the ladder
with its two soft clippers is 58 cycles per sample, a saw oscillator 12 to 15, a pulse 25 (two band-limited edges).
Osc 3 costs nothing when its level is 0.

## What is left to try (each with a cost in sound or in code)

- **Drop the output soft clip** (8 cycles): v1 had it, it keeps resonant peaks and self-oscillation below full scale
  and adds a little warmth; without it about 1 % of samples clip in the most extreme settings.
- **Update the filter coefficients per 64 samples instead of 32** (about 2 cycles): the contour's attack gets a
  coarser staircase (1.5 ms steps).
- **A cheaper pulse**: one band-limited edge and a naive one (aliasing on the falling edge) or a pulse from a
  band-limited square table.
- **Hand-scheduled assembly for the ladder loop** (dual issue: a multiply with two loads / an ALU op per instruction):
  perhaps 58 -> 40 cycles, at the price of readability.
- **Code size**: what the first voice in each callback pays in cache misses depends on the bytes the hot path
  touches; the oscillator loops are specialised per wave, so only one of them is fetched per oscillator.
- **Eco mode** for unison: done for osc 3 in v2.1 (the CPU guard: off from 4 sounding voices, with a fade, shown on
  the screen). Further steps if needed: osc 2 off as well from 6 voices, or a 2-pole filter.

How many voices fit on the OP-1 can only be measured there (resource monitor: clock screen + T1). Comparing with
supersaw on the same patch (FX, master FX, tape playing) would calibrate these numbers.

## v3 (`minimoog3`): views, noise, drive, feedback, AC coupling

| v3, cycles per sample per voice | |
|---|---|
| CLASSIC (2 saws) | 117 .. 124 |
| FAT BASS (3 saws) | 134 .. 141 |
| NASAL (3 pulses) | 189 .. 198 |
| BREATH (2 saws + noise) | 128 .. 134 |
| 1 SAW | 100 .. 113 |
| supersaw (reference) | 83 |

(Ranges: the same code measured in two builds; the simulator is sensitive to code layout at this level.) About 20 %
over v2 at the defaults, for the drive (a saturating shift), the feedback path (only in a second loop used when
feedback is up), the AC coupling (two operations), oscillators with their own waves (pairs of the same kind share a
pass) and the parameter handling per block. Two traps found on the way, both in the compiled code: a library division
for the semitone split (now a multiply), and GCC putting the Blackfin hardware loop on the 4-chunk loop instead of the
32-sample filter loop (the sample loop is now a function of its own).

## v3.2 (`minimoog3`): audio paths

The engine picks its code per block (oscillators) and per 32-sample chunk (filter) from what is switched on, so a
patch pays only for what it uses. Every path is a specialised loop, not a branch inside the sample loop: the extra
cost is code size and maintenance, so each one must earn its place in `make bench`.

| stage | paths | chosen by |
|---|---|---|
| oscillators | one pass per sounding oscillator, specialised per wave kind (saw, pulse, triangle, shark mix) | wave |
| | two neighbours of the same kind (1 + 2 or 2 + 3) share one pass | waves |
| | skipped: range OFF, volume 0, above 16 kHz; osc 3 while the CPU guard rests it (4+ notes, unison) | settings, guard |
| noise | its own pass, only when the noise level is up | noise > 0.2 % |
| filter | **eight** sample loops (v3.3): with / without the feedback path x resonance x saturation | per chunk |
| | without resonance: no state sum, no feedback solve (k = 0 when emphasis is below 0.8 %, and in the open range) | emphasis, cutoff |
| | with feedback: the AC-coupled output added to the ladder input | feedback > 0 |
| | with saturation (v3.3): gain, bias, a second soft clip, DC blocker and (v3.4) a one-pole low-pass in the output stage, +20 cycles | SAT > 0.2 % |
| scope | one voice only (the latest note), 64 word copies per block; decimated copies for long cycles | screen |

| v3.2, cycles per sample per voice (simulator) | |
|---|---|
| CLASSIC (2 saws) | 126 |
| CLASSIC, emphasis 0 | **113** (resonance-free loop: -13) |
| FAT BASS (3 saws) | 143 |
| BREATH (2 saws + noise) | 137 |
| ACID (1 saw) | 115 |
| supersaw (reference) | 83 |

The resonance is the costly part of the ladder (the four-state sum and the solve): a patch with emphasis at 0 (many
basses, pads and brass) is now 10 % cheaper. To add a path: a constant flag in `filter_chunk_t` (or a new pass
kind), one `noinline` instance per combination (the sample loop must keep the Blackfin hardware loop and the
registers to itself), the choice in `mm_render`, a scenario in `tools/bench/bench.c`, and this table.

Tried and dropped (no gain beyond the simulator's +-5 cycle layout noise, so not worth the code):

- **Reusing the filter coefficients** while the cutoff does not move (sustain, a still knob): `lad_coef` costs about
  3 cycles per sample; skipping it saved 2, and the cached copy per voice costs memory and a compare per chunk.
- **Pairing oscillators 1 and 3** when they are the same kind and 2 is not (FUNK BS: square, saw, square): 180 vs
  179 cycles; the pulse pass gains little from sharing.

## v3.3: saturation

SAT (analog view) adds a saturation stage after the filter in its own loops, so a patch without it pays nothing:
CLASSIC with SAT up measures 144 cycles per sample against 131 without (+12 in the filter loop). LO is gone (it only
cost a range step). Note on the numbers: CLASSIC's output is bit-identical to v3.2's (same hash) but measures 131
here against 126 there: the same computation, laid out differently by the compiler (the +-5 noted above).

## v3.4: a less digital saturation

The cubic soft clip itself is smooth, but at 8x on a bright filter output its harmonics go past the audio band and
fold back (aliasing): that is the "digital" grit. Two fixes were measured:

- **ADAA** (antiderivative anti-aliasing of the cubic: the clip of the mean over each sample, s (1 - q / 3)) removes
  most of the folding and is division-free for a cubic, but needs the previous input and two more products: the
  loop ran out of registers and spilled, 160 .. 163 cycles. Dropped.
- **A one-pole low-pass after the clip**, from about 16 kHz at low SAT down to 5 kHz at full SAT, as the output
  stage and speaker of an overdriven amp: it takes the fizz (5 .. 10 kHz 4 .. 7 dB lower at full SAT, level unchanged)
  and costs 7 cycles. Kept, with a little more bias (up to 0.15 of the clip level) for warmer even harmonics.

| v3.4, cycles per sample per voice (simulator) | |
|---|---|
| CLASSIC (2 saws) | 132 |
| CLASSIC, emphasis 0 | 119 |
| CLASSIC, SAT up | 152 (+20: clip, bias, DC blocker, low-pass) |
| FAT BASS (3 saws) | 149 |

The other scenarios' differences from v3.3 are within the +-5 layout noise.
The LFO needles (the screen shows where the LFO takes each knob) cost the audio path one store per knob per block.

## v3.5: the transistor stages

What the analog-modelling ladders do (Huovilainen, DAFx 2004, in Csound's moogladder; D'Angelo & Valimaki, ICASSP
2013; VCV Rack's VCF): every stage is a differential pair, so each one is tanh(in) - tanh(state), not only the
input. v3.4 clips the input only. Compared with a float reference (all stages tanh, solved implicitly, 8x
oversampled), on the harmonics of a saw through the filter: clean (input at the clip level) v3.4 is within 2 .. 3 dB;
driven 3x it is off by 5 .. 11 dB, mostly too dark above the cutoff (a driven transistor ladder gets brighter and
buzzier, not just compressed). Clipping all four stages brings that to about 4 dB; clipping only the first one or two
does not help. (A "leak" of the input around the ladder, the branch `leak`, was further from the reference than
v3.4 itself: no model does that, and on real units it is a calibration fault.)

Cheap form: each stage takes clip(in) - t, with t its own clipped output one sample ago (the state's clip, delayed
by a sample): 4 clips a sample instead of 7, as close to the reference as the full form. A 5-instruction cubic (the
same x - x^3 / (3 L^2), the saturating shift as its clamp) instead of the input's 9. The delayed t is only accurate
while the stages move little per sample: above 2 .. 3 kHz it adds a false bump near the top (6 .. 15 dB too much
above 2 kHz at a 5 kHz cutoff: the "a bit much" on a driven SAW LEAD swell in the first candidate, which ran up to
8 kHz), and the ladder blows up above about 11 kHz (fuzz). So the stages run below 2 kHz, where they are 1 .. 3 dB
from the reference against v3.4's 3 .. 12 when driven.

Only when driven (DRIVE above 50 % since v3.6, 10 % in v3.5: clean or lightly driven, the two agree within a few
dB, the 8 .. 12 dB gap is at 2.5 .. 4x) and below 2 kHz; the level is the same
(+-0.5 dB). Cost while they run: CLASSIC driven (75 %) at a low cutoff 129 -> 172 cycles per sample (+43: the loop no
longer fits the registers, so it loses the hardware loop too); nothing when clean or above 2 kHz (CLASSIC at drive
50 % with the contour opening it: 130). 3.9 kB of code for the eight loops.

## v3.7: the resonance's whistle

The input clip is also the resonance's limiter (u = clip(x - k S), the feedback inside the clipped sum, as on the
real ladder). Until v3.6 the passband make-up for the resonance was an input gain (up to 2.4x), so the more emphasis,
the harder the input clipped and the more the whistle was crushed. In a float model of the engine's ladder (saw at
110 Hz, cutoff 880 Hz, emphasis k 3.5) the harmonic at the cutoff stood 28 dB under the bass with the make-up at the
input, 11 dB under with it at the output, 3 dB under with the input 8 dB cooler as well. Now: input 4 dB cooler
(IN_LVL), the make-up and the 4 dB back as one gain after the AC coupling (before the output stage's soft clip, so
the output still cannot hard-clip). In the engine (CLASSIC, A2, cutoff ~880 Hz) the cutoff's harmonic against the
bass: emphasis 75 % -14.9 -> -10.7 dB, 85 % -12.8 -> -5.1, 94 % -9.2 -> +2.2; the bass level is unchanged. DRIVE's
level table is recalibrated for the cooler input (the same loudness against drive as v3.6, +-1.5 dB per patch).
Cost: one multiply and a shift per sample, +3 cycles (CLASSIC 129 -> 132).

## v3.8: the hardware loop back in the driven loops

An audit of the compiled loops (v3.7): no division anywhere in the audio path (only in the screen and knob code), no
branch in any filter loop (every option is a constant in its own loop; the oscillators keep their one unlikely
branch near an edge), 0 .. 9 memory accesses beyond the samples per loop. But three of the sixteen filter loops
(transistor stages with resonance or saturation) had lost the Blackfin hardware loop: GCC inserts it, then runs out
of registers and spills the loop counter, which turns it back into a decrement, store, compare and branch. Freed
three data registers there: the stage clip takes its constant from the input clip's (CLIP_K, x 4 by a shift: one
instruction more per stage, one register less), and two pairs of 16-bit coefficients share a register each (the
saturation's gain and low-pass; the output make-up and the feedback), as gain | level already did. Fifteen of the
sixteen loops now keep the hardware loop (the last, feedback + SAT + transistor stages without resonance, is one
register short). Driven at a low cutoff: 176 -> 169 cycles; every other path gives v3.7's output bit for bit.

## v3.10: LO and the modulation

LO is back as a range (an octave shift and a wider frequency knob: no cost of its own; a LO oscillator in the mix is
a pass like any other). The modulation runs per block, outside the sample loops: osc 3's shape at five phases (four
chunks and the block's end), a random value per chunk for noise, then for the pitch two increments (start and end of
the block: the oscillators' ramp) and one polyBLEP reciprocal, and for the filter an offset per chunk on the cutoff
position (smoothed as the contour is). With osc 3 in LO and silent, its phase is advanced by the block's sum instead
of a pass. CLASSIC with a LO vibrato and wah measures 141 cycles per sample against 132 (+9), only while a depth is
up; every other bench scenario is bit-identical to v3.9 (same hashes; the new scenario runs last so the others keep
their voice state).

## v3.11: presets

The presets cost nothing of their own: an entry is derived once when the setup word changes (the same derive as a
setup, plus the contour amount's ratio), and the render path only looks the entry up. Every scenario above is
bit-identical to v3.10. They do reach the expensive paths, though: the library's patches are written for a driven
Minimoog, 28 of the 94 drive above 50 % (the transistor stages, +43 cycles) and most use saturation (+12 .. 20). The
bench now runs every preset (16 blocks each after the attack): average 171 cycles per sample per voice, the costliest
P73 (number 008 on the knob in v3.13; 8' saw, 4' square, 4' saw, drive 62 %, saturation 43 %) 236, against 210 for the costliest setup (OBOE). The
osc 3 CPU guard applies as for setups.

## v3.14: Chase presets, the library retuned

Four presets more (99 stored, 89 on the knob with 10 retired) and new cutoffs / oscillator ranges for 26 library
patches; nothing in the render path changed. Every scenario's hash is identical to v3.13; the preset sweep (all 99)
changed as it should: average 170 cycles per sample per voice (171 before), the costliest still P73 (now number 012)
at 236.

## v3.15: the near-duplicates removed

The 10 retired patches are gone from storage (89 presets: Jestofunk and Chase first, then the library). Every
scenario's hash is unchanged; the preset sweep (all 89): average 170, the costliest P70 (number 115, the same patch as
P73 before) at 236.

## v3.16: the LFO counts twice

The OP-1 LFO's offset on a knob is doubled in the block's parameter setup (once per block, not per sample). Every
scenario's hash is unchanged (their knobs carry no LFO offset); host == Blackfin.

## v3.17: views redrawn

Screen only (advanced, oscillator and analog views). The analog view's traces are the sound's own oscillator waves
through the engine's cubic soft clip, computed in the GUI thread (at most 3 x 99 points a frame). Every scenario's
hash is unchanged; host == Blackfin.

## v3.18: volumes and noise on the views

Screen only. The analog view draws each oscillator at its volume and the noise (one more trace of 99 points). The
oscillator view's idle shape follows the volume, and it also comes back once the scope ring stops moving (the
firmware no longer renders the voice), not only when the contour has faded. Every scenario's hash is unchanged;
host == Blackfin.

## v3.19: a setup is a whole sound

Turning to a setup writes its own cutoff, emphasis, contour, detune and drive, sets its saturation and feedback, and
resets the drift, as a preset does (in the GUI thread, once per turn; the render path is unchanged). Every scenario's
hash is unchanged but two: ACID and SHARK, whose saturation / feedback are now scaled as a preset's (pct: x 327.67
instead of x 327, 0.2 % more). The preset sweep is identical; host == Blackfin.

## v3.20: home is a scene

Screen only. The home view draws a landscape of line segments: three objects (84 .. 118 lines each at the front slot,
a coarser copy of 52 .. 105 for the middle and back slots), the far mountains, the grass, the water surface and the
top bar. A frame is about 320 .. 480 lines (the busiest: three cars in the wind), against 460 .. 500 on the views with
the modulation row. The objects are tables (moai_scene.h, 6.4 kB, from plugin/scene/*.svg by gen_scene.py);
lines behind an object are cut by its silhouette (18 bands of x extents per object, 216 bytes), lines below the water
split once at the surface. No division per point. The audio path is unchanged: every scenario's hash is unchanged;
host == Blackfin.

## v3.21: home, the moai

Screen only. The home scene is one drawing (moai_scene.h, 130 strokes, 334 lines, 2.3 kB, from
scene/moai_v3c.svg by gen_scene.py; the six objects' tables are gone: the plugin is 62 kB instead of 70). Each stroke
asks the filter's response once (130 calls of mg_resp at most, none once the strokes are 1.5 octaves above the cutoff)
and draws at most 334 lines: a frame is about 270 .. 405 lines (closed: 272; open, or Shift: 401 .. 405), against up to
483 for the v3.20 scene. The audio path is unchanged.

## v3.22: home, a ridge, the number

The audio code is unchanged; one preset is new: 036 (was the siren, CSV 65) is a French touch chord sound made here,
so the preset sweep's hash changes (average still 170 cycles per sample, the costliest P70 236); host == Blackfin.
The home view adds the filter's ridge (19 lines and 19 calls of mg_resp), larger knobs (12 segments each when idle)
with their names always shown, and the number in the stock font (one text object per colour, built once, re-laid out
only when the number changes; the host previews draw line digits instead, about 30 lines). A frame is about
360 .. 490 lines in the previews (about 30 fewer on the OP-1).

## v3.23: home at rest

Screen only. The number (stock font: five text objects, four levels of the fade and blue) and the knobs show only
while they change, then fade; the knobs are drawn thicker (16 segments). The front ridge is the response without the
emphasis (17 calls of mg_resp), the summit behind it 6 lines; the island follows the contour over time (24 lines, as
up to v3.19). A frame is about 280 .. 485 lines in the previews (at rest about 340; Shift, all four knobs: 485).

## v3.24: home fixes

Screen only. The fades count frames (the screen's dt is not documented in ms; the fades of v3.23 relied on it). The
summit is a fixed height and as wide as the emphasis: up to 10 lines, over its widest outline (up to 6). The last
knob's colour on the strokes at the cutoff is a mix, no extra lines. A frame is about 290 .. 492 lines in the
previews (Shift, all four knobs: 492).

## v3.25: home ridges

Screen only. The left horizon is three ridges from 19 samples each (57 calls of mg_resp: the front at the sound's
cutoff, the knobs' response with the emphasis, the contour's reach), up to about 50 lines, drawn where each shows. The
number's stock-font fade has three levels in white and three in blue (six text objects, bss 4112 B). A frame is
about 300 .. 503 lines in the previews (Shift, all four knobs: 503).

## v3.26: the top keys under pitch modulation

Per block only, the sample loops are unchanged. The pitch preparation (osc_prep, cents_inc: interval, fine tune, the
pitch modulation's octave) works on increments up to 2^31: at MIDI 127 an interval or +1 octave of modulation passed
it (a signed overflow). The increment is now scaled unsigned and held at INC_SAT, above the 16 kHz limit that leaves
an oscillator off by more than fine tune and detune move it; a downward modulation shifts the octaves down first.
Every scenario's hash is unchanged but the two with pitch modulation (osc 3 LO: vibrato + filter; the preset sweep):
the downward shift now comes before the interval, a last-bit difference in the increment. Average 171 cycles per
sample per voice over the presets (170 before: a few instructions per block), the costliest P70 236; host ==
Blackfin.

## v3.26: the audio code in one place

On the OP-1, three held notes crackled in v3.25 (CPU spikes, on any view), not in v3.11 or v3.20, with the same audio
code (v3.19 on). In the simulator all three do the same work: the sample loops are the same instructions, and one
callback with three notes (preset 0, `make cachetrace`) touches about 160 lines of code (5 kB) and 50 of data in each.
What differs is where that code lies, which the simulator does not price:

- the loader reads the whole file (the SVG first) into a heap block and allocates the plugin while that block is still
  there (plugload.c), so the plugin's address follows the file's length;
- GCC orders the functions of one file its own way: the audio functions were spread over 21 kB among the screen's.

So every screen change moved the audio code against the firmware's in the caches: the likely cause (the simulator
cannot see the firmware's side), a lottery that v3.11 and v3.20 won. The audio is now its own compilation unit (moai.c; the GUI thread is moai_ui.c, what they
share moai.h, the tables kept once), so the image starts with it, whatever the screen code grows to:

| one callback, three notes | v3.11 | v3.20 | v3.25 | v3.26 |
|---|---|---|---|---|
| code lines touched (preset 0) | 161 | 162 | 162 | 161 |
| ... spread over | 21.2 kB | 23.0 kB | 21.6 kB | 12.0 kB |

The first 12 kB of the image are what a callback runs: the per-block helpers, the eight filter loops without the
transistor stages, mm_render; the loops and mm_render start on a 32-byte line. The transistor-ladder loops (DRIVE's
top half: then 167 lines a callback, 15 of them the filter) and what runs per note or when the sound changes (`cold`) lie after the GUI
code. Every scenario's hash is unchanged; cycles per sample the same or lower (OBOE 215 -> 203, the others by 0 or 1;
presets: average 170, was 171); host == Blackfin.
The image is 48.3 kB + 4.1 kB bss.

The address still follows the file's length. tools/op1p_pad.py pads a file to a given length and memory size (NULs
after the SVG, a larger bss: the format allows both), so that two builds load at the same address.

