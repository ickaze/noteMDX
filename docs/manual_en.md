# notemdx.exe 0.8 — MML reference

notemdx.exe 0.8 is an MML converter that generates MDX for X68000 MXDRV2. This manual organizes the technical explanations from note.doc with an index. Where the current implementation differs, the notes in each section take precedence. The complete original is included as note.doc (CP932 text).

## Usage

```
notemdx.exe [options] input.mml
notemdx.exe -c -z STAGE5.mus
notemdx.exe --encoding utf8 --output song.mdx song.mus
```

Input extensions may be omitted: `.mml` is tried before `.mus`. By default, the output uses the input name with `.mdx`. One input is accepted per invocation. Original no-argument switches may be combined, for example `-xpr`. String arguments for -i/-z/-t/-w consume the remainder of their switch token.

|Current option|Meaning|
|---|---|
|`--output FILE`|MDX output path. The older -o FILE alias is accepted only when distinguishable from the original no-argument PCM-map switch.|
|`--encoding auto`|Default: UTF-8 with BOM; otherwise CP932.|
|`--encoding utf8` / `--encoding cp932`|Explicit encoding, also used for included MML.|
|`-x` / `--reverse-octave`|Reverse the initial meaning of `<` and `>`.|
|`-p` / `--ex-pcm`|Enable Q–W in addition to P, for eight PCM tracks.|
|`-iCHANNELS` / `--mute CHANNELS`|Exclude selected tracks, e.g. ABPQ; lowercase is accepted.|
|`-c` / `--compress rests`|Merge eligible adjacent rests.|
|`-cn` / `--compress notes`|Also merge eligible tied notes of the same pitch.|
|`--compress none`|Explicitly disable compression.|
|`-z` / `--opt "*"`|Enable all optimization categories.|
|`-zSELECTORS` / `--opt SELECTORS`|Select from dvqpt012; `--opt none` disables optimization.|
|`--help` / `--version`|Show help or the current version.|

Compression and optimization default to off. Explicit CLI settings override MML directives throughout the input, including includes. Compression is not used inside tuplets, waveform effects, portamento or glide processing. Note compression may change gate behavior. Optimization removes or replaces redundant settings, including settings before the next note and some settings separated by rests. Exact equivalence to every original optimization rule has not been established.

|Selector|Commands|
|---|---|
|d|D|
|v|v, @v|
|q|q, @q|
|p|p|
|t|@|
|0|MD|
|1|MP|
|2|MA|

Exit codes are 0 for success, 1 for MML conversion errors and 2 for argument or I/O errors. Conversion errors normally preserve existing MDX; -r explicitly deletes it and -e explicitly saves requested banks at the first error. Titles and PDX names in MDX use CP932. Characters that cannot be represented are errors. Relative include and bank paths resolve from the file containing the directive. Output paths must not overwrite any input.

### Original switches: implementation status

|Switch|Current behavior|
|---|---|
|`-mSIZE`|Sets the complete-file budget in KiB: 1–65536, default 64. Revision 2 reproduces the 67,488-byte K01TAIL reference with -m256. Track and voice-start offsets must still fit their separate 16-bit fields.|
|`-x`, `-p`|Octave reversal / EX-PCM, supported.|
|`-iCHANNELS`|Original channel mask restored; --mute is retained as an alias.|
|`-r`|Delete existing MDX after a conversion error. Inputs, included sources and loaded banks are protected from deletion.|
|`-b`|Beep on conversion error: Windows Beep or terminal BEL elsewhere. Audibility depends on the environment.|
|`-l`, no-argument `-o`|Recognized with an unsupported-feature warning. No pcmuse.map is read, generated or OR-merged. -o without -l also warns about the missing prerequisite.|
|`-c[n]`, `-z[dvqpt012]`|Compression / optimization, supported.|
|`-t[name]`, `-w[name]`|Save voice / waveform bank. Default tone.bin / wave.bin. CLI names override #save-tone/#save-wave and resolve from the working directory. Attach the filename directly, e.g. -wcustom.bin.|
|`-v`, `-v0`|Show the channel being converted and the error line. Color from the error position on supported terminals; use an ASCII arrow for redirected or unsupported terminals.|
|`-v1`|Show conversion channels and insert ASCII -> immediately before the error position in the source line.|
|`-1`|Stop at the first fatal error. Current parsing also stops there without this switch.|
|`-e`|Save requested banks even after conversion errors. Only data registered before the first error is saved; parsing does not recover and continue afterward. Partial MDX is never saved. PCM maps remain unsupported.|

For an unambiguous MDX output path, use --output. The compatibility -o FILE form is recognized after an input is already specified, when FILE ends in .mdx/.MDX, or when output and input names are consecutive arguments. Use combined -lo for the original PCM-map request. #remove/#beep/#ver remain ignored with warnings; use the CLI switches above. #play is not executed. See CLI_COMPATIBILITY.md for the checks and limitations.

## Conversion report

All report labels are English. User-provided titles and filenames retain their original text. Example layout:

```
Title       : Example
FM voices   : 0, 1, 2
PCM bank0   : 0, 2
OPM track   :      A      B      C      D      E      F      G      H
Total steps :    192    192      0      0      0      0      0      0
Loop steps  :    144      -      -      -      -      -      -      -
PCM track   :      P      Q      R      S      T      U      V      W
Total steps :    192      0      0      0      0      0      0      0
Loop steps  :    192      -      -      -      -      -      -      -
```

Labels occupy the 11-character width of Total steps, followed by one space and a colon. FM lists every selected voice included in the MDX voice bank individually, without numeric ranges. Unused FM and PCM numbers are no longer displayed. Selection before a note is still counted as FM use. PCM shows note slots 0–95 actually used within each bank. A bank is listed only if a note uses it; selecting a bank alone does not list it. This does not check whether samples exist in a PDX file. Consecutive PCM numbers may be abbreviated as ranges. PCM labels are PCM bank0 through PCM bank255.

A step is 1/48 of a quarter note. Total steps expand finite repeats, including nested repeats and last-pass escapes, and end at the first track terminator. An infinite L loop is counted once for this total. Loop steps run from the first executed L or C to the terminator; C only marks a counting position and does not create a playback loop. A C in a branch never executed has no loop count. No loop marker is shown as `-`; empty, disabled and muted tracks have total 0. W synchronization waiting is not included. Gate shortening does not reduce the track's elapsed step count. Counts exceeding unsigned 64-bit capacity display `overflow`, without expanding a huge repeat in memory. The same data is available through `Result::statistics`.

## Line identifiers and comments

Except inside a continued multiline definition, the first character identifies a line. Leading spaces, tabs and other invalid identifiers make the whole line a comment. Valid identifiers are A–H for OPM, P–W for PCM, # followed by a directive, @number for an FM voice, @wnumber for a waveform, @knumber for a key-to-voice map, @@number for a tone macro, or a macro variable name for a definition.

Several track identifiers may share a line: `ABC c4 d4 e4` sends the same MML to A, B and C. Duplicate identifiers have no extra effect: `AAA ceg` is equivalent to `A ceg`.

Inside a music line, `//`, `/*`, `*` and `;` start a comment extending to the physical end of the line; `/*` is not a C-style multiline comment. An uppercase letter that is not a valid command or macro likewise starts a line-end comment. Valid command operands, macro syntax and `$` / `#` commands are parsed according to their own grammar. Invalid `$` / `#` commands are not silently accepted as comments.

## MML commands

In the following syntax, square brackets mean optional text and angle brackets mean required text, except where the actual MML command is a bracket or octave sign. Parameter placeholders are not literal percent signs unless explicitly described as step notation.

### Notes, lengths and control flow

|Command|Meaning|
|---|---|
|`a`–`g` followed by optional length|Named note; see Notes and pitch.|
|`n<pitch>[,length]`|Numeric pitch, -24 (o-2d+) through 119 (o10d) before transposition. Final encoded pitch must be 0–95.|
|`r[length]`|Rest.|
|`{notes}[length]`|Tuplet: divide the specified total equally among up to 32 notes/rests. The total may exceed 256 steps, but a resulting element may not. Remainders follow rounded cumulative boundaries. No nested tuplets, loop commands or waveform effects. Glide does not apply; no target duration after portamento.|
|`&`|Tie adjacent notes/rests. Use immediately after a note/rest; the original text allows a continued-line tie when adjacency is valid and notes that a line-leading tie during waveform processing may be ignored. Current compatibility does not cover every cross-directive tie. Rest ties maintain waveform phase without emitting a note-tie opcode for the rest.|
|`_[o or < or >]<pitch>[length]`|After a note, linearly slide to the target pitch during that note's duration. Only o, < and > may precede the target. A shorter target duration is ignored; a longer one holds the final pitch for the excess. Waveform processing is temporarily interrupted.|
|`_D<delta>`|After a note, slide by -6144…6144 units of 1/64 semitone during that note. Temporarily interrupts waveform processing.|
|`[body]count`|Finite repeat; count 1–255, default 2. Up to 32 nested repeats.|
|`/`|Within a repeat, skip everything after this point on the final pass.|
|`L`|Infinite-loop start. Cannot be combined with C or used inside a finite repeat.|
|`C`|Counting-only loop marker; playback still ends normally. Cannot be combined with L or repeated. It also prevents compression across the counting boundary.|
|`|part:part:...|`|Distribute MML among the selected channels in channel order. See Channel-specific content.|
|`｢notes｣[length]` or paired backticks|Distribute chord notes/rests among selected channels.|
|`#[length]`|Reuse the preceding chord; empty if none has been defined.|
|`!`|Stop converting the current channel's remaining MML.|
|`? ... ?`|Suppress emitted commands while still updating parsing state such as octave and default length. Driver state is not updated, so reset controls after the range as needed. Repeats must not cross a suppression boundary. Current L inside suppression is rejected; do not assume all original permissive combinations are supported.|

### Tempo, tone, pitch and gate

|Command|Meaning / range / default|
|---|---|
|`t<bpm>`|Quarter notes per minute: 19–4882.|
|`@t<value>`|OPM timer B: 0–255.|
|`@<number>`|OPM voice 0–255, initially unspecified; PCM bank 0–255, initially 0.|
|`SMON`, `SMOF`|Enable (default) or disable automatic tone-macro expansion after voice selection.|
|`KS<map>`|Enable pitch-based automatic voice selection using map 0–7. Does not expand tone macros. Voice commands in this region are optimized automatically.|
|`KSON`, `KSOF`|Resume or stop automatic voice selection.|
|`o<octave>`|Octave -2…10; initial o4.|
|`<`, `>`|Lower / raise octave by one unless reversed.|
|`TR<value>`|Per-channel transposition -48…48; initial 0. Applies to PCM too, and adds to #tps where enabled.|
|`$FLAT{notes}`, `$SHARP{notes}`|Set a flat or sharp key signature for named cdefgab notes on this channel.|
|`$NORMAL{notes}`, `$NATURAL{notes}`|Clear that signature. Omitted braces/notes clear all seven pitches.|
|`D<value>`|Detune -32768…32767 in 1/64 semitone units; initial 0.|
|`l<length>`|Default duration; initial l4.|
|`q<value>`|Sound 1–8 eighths of a note; initial q8.|
|`@q<steps>`|Key off 0–192 steps early; 0 is q8. The last q/@q setting wins.|
|`Q<value>`|Waveform-only gate: 0 returns to q/@q; 1–256 selects a fraction in 1/256 units; -256…-1 uses the absolute number of sounding steps, capped at the note duration.|
|`k<steps>`|Key-on delay 0–255; initial 0.|

### Volume, pan, registers and PCM

|Command|Meaning / range / default|
|---|---|
|`v<value>`|16-level volume, 0–15; initial v8.|
|`@v<value>`|128-level volume, 0–127. Last v/@v wins.|
|`x<value>`, `@x<value>`|Use a temporary volume for the next note only, in the respective 0–15 / 0–127 scale; restore at the next ordinary note.|
|`([value]`, `)[value]`|Decrease / increase driver volume by the given amount; omitted amount means one.|
|`V<delta>`|Add to the compiler's current volume and emit v/@v, clipped to range. Does not account for driver-side (/) changes; see Relative volume.|
|`VO<delta>`|Offset for v/@v/x/@x, -127…127; initial 0.|
|`p<value>`|Pan: 0 mute, 1 left, 2 right, 3 center; initial 3.|
|`y<register>,<value>`|Write OPM register, both 0–255. Decimal, $hex or %binary; binary may contain underscores, e.g. %11_000_101. The original syntax recognizes at most two hex digits or eight binary digits for the value; the register field has no digit-count restriction but must fit its range.|
|`w[value]`|OPM noise frequency 0–31; omitted value stops noise.|
|`S<channel>`|Send synchronization signal to 0–15 or A–H/P–W.|
|`W`|Wait for a synchronization signal.|
|`K`|Immediately cut OPM reverb; normally reselect the current voice afterward, unless #noreturn.|
|`F<value>`|PCM sample rate/mode: 0=3.9, 1=5.2, 2=7.8, 3=10.4, 4=15.6 kHz (default). 5=16-bit PCM/15.6 kHz, 6=8-bit PCM/15.6 kHz, requiring PCM8 v0.46+ in the original runtime. Values 7–31 are accepted for PCM8++.|
|`$FO<value>`|Fade all channels, 0–255; smaller values fade faster.|

### Hardware and software LFO

`MH<wave>,<frequency>,<PMD>,<AMD>,<PMS>,<AMS>,<sync>` sets and enables the OPM hardware LFO. Wave 0 is sawtooth, 1 square, 2 triangle, 3 sample-and-hold/random. Frequency is 0–255; PMD and AMD are 0–127; PMS is 0–7; AMS is 0–3. Sync 0 leaves the phase running; sync 1 resets at each key-on. MHOF stops it, MHON resumes it, and MHR forces a phase reset.

`MP<wave>,<quarter-period>,<amplitude>` sets and enables the software pitch LFO. Waves 0–3 are sawtooth, square, triangle and random. Adding 4 selects the corresponding 256-times pitch-amplitude mode in the driver. The second argument is the number of steps in one quarter period. Amplitude units are 1/64 semitone, like D1. MPOF stops and MPON resumes.

`MA<wave>,<quarter-period>,<amplitude>` sets and enables the software volume LFO, with waves 0–3 and amplitude in @v1 units. MAOF stops and MAON resumes. `MD<steps>` delays software LFO start after key-on; 0 disables synchronization between key-on and LFO start. Current parameter checks and byte-encoding details are documented in COMPATIBILITY.md.

### Glide

`GL<delta>,<steps>` enables glide: during the first 1–255 steps of each eligible note, slide from delta back to zero, then hold the original pitch. Delta is -6144…6144 in 1/64 semitone units. GLOF stops and GLON resumes. Glide does not apply to PCM, tuplets, notes with portamento, or numeric n notes. Waveform processing pauses during the glide. See the Glide details section for expansion examples.

### Wave-memory commands

All eight effects use `<effect><wave>,<interval>,<mode>` to select and enable a waveform. Wave numbers are 0–127. Interval specifies steps per element in modes 0/1, or notes per element in mode 2. Mode 0 is asynchronous by time, 1 is synchronized to notes, and 2 is asynchronous by note count.

|Effect|Target|
|---|---|
|AP|Automatic pan. Changes inside a PCM note do not affect that note in the original driver.|
|DT|Detune: add waveform values to the current D.|
|TD|Second detune effect, independent of DT, with the same subcommands.|
|VM|Volume relative to the last v or @v scale.|
|MV|Second volume effect, independent of VM, with the same subcommands.|
|KM|PMS/AMS together: waveform values are bytes for OPM registers $38–$3F. Set PMD/AMD using MH first.|
|TL|Operator total level relative to voice data; see TL details.|
|YA|Raw register writes: high byte of each waveform value is register number, low byte is the value.|

Every family supports ON (resume), OF (stop), D followed by a delay, and L followed by a new element interval. Thus APD/APL/APON/APOF, DTD/DTL/DTON/DTOF, TDD/TDL/TDON/TDOF, VMD/VML/VMON/VMOF, MVD/MVL/MVON/MVOF, KMD/KML/KMON/KMOF, TLD/TLL/TLON/TLOF, and YAD/YAL/YAON/YAOF all exist. A delay is -255…32767, initially 0. An interval change takes effect after the current interval finishes, preserving phase; its effect may not be immediate.

DTS/TDS multiply detune waveform values; VMS/MVS multiply volume waveform values. The initial multiplier is 0, interpreted as 1. Avoid excessive values that would overflow the intended target. Using both DT/TD or VM/MV can generate redundant output; optimization is useful.

`TLM<mask>` selects operators for TL, bits 0–3 for operators 1–4, range 0–15. The default -1 automatically selects modulators using the voice definition available at that point. A carrier's level is based on the voice's original TL and does not incorporate v/@v changes; normally select modulators. `TLT<voice>` changes the voice definition used as a TL reference without selecting that voice for playback. Its voice must already be defined.

`zc<0..2>` changes #cont within MML; `zw<0..2>` changes #wcmd.

### Macro calls

`$<variable>[subscript]` expands a macro; undefined macros are empty. I, J, N, O, X, Z, h, i, j, m, s, u and permitted half-width kana may omit $. The excluded kana punctuation is ｡ ｰ ﾞ ﾟ ｢ ｣ ･ ､. See Macro definitions for the larger set allowed in definitions.

## Directives

Directives start at column 1 and are case-insensitive. Put whitespace between a directive and its argument. Settings generally take effect from their position onward; definitions and global metadata have the behavior described below.

|Directive|Meaning|
|---|---|
|`#title "text"`|MDX title; last setting wins.|
|`#pcmfile "name"`|PCM/PDX filename stored for playback. Does not load or verify the sample file.|
|`#include "file"`|Insert another MML file here, up to 16 levels.|
|`#ex-pcm`|Enable Q–W.|
|`#octave-rev`|Reverse the direction of < and >.|
|`#tps value`|Transpose -24…24 semitones on all OPM tracks; PCM excluded unless #tps-all.|
|`#tps-all`|Make subsequent #tps processing apply to PCM too.|
|`#detune value`|Offset -32768…32767 added when a D command is used; does not itself issue D.|
|`#flat "notes"`, `#sharp "notes"`|Set key signatures for cdefgab, globally.|
|`#natural ["notes"]`, `#normal ["notes"]`|Clear global signature entries; omitted notes means all.|
|`#nlist`, `#list`|Start an ignored text region / resume conversion.|
|`#overwrite`|Allow later voice definitions to replace earlier definitions of the same number.|
|`#toneofs value`|Offset applied to both voice definitions and voice selection.|
|`#noreturn`|Disable automatic voice reselection after K.|
|`#wavemem`|Declare waveform memory, originally about 128 KiB for 128 waves; required before waveform definitions and use.|
|`#reste [0 or 1]`|Apply effects during the gated-off tail too; 1 also processes rests. For synchronized rest phase continuity use a tie or #cont 2.|
|`#nreste [0 or 1]`|Apply effects only while sounding. Nonzero restores the pre-effect state at key-off; omitted/0 leaves the changed state.|
|`#coder`, `#ncoder`|Fill missing chord parts with rests / leave them empty.|
|`#glide [0 or 1]`|0 or omitted: glide on tied continuation notes too. Nonzero: only the first note of a tied group.|
|`#load-tone "file"`|Load voice registration and data, replacing existing registered voices. Requires a 7168-byte NOTE bank.|
|`#save-tone ["file"]`|Save final voice registration and data. Default tone.bin; all registered voices are saved, including unused voices.|
|`#load-wave "file"`|Load waveform registration and data, replacing the current state at that position in each channel. Requires a 131968-byte NOTE bank.|
|`#save-wave ["file"]`|Save final waveform registration and data. Default wave.bin.|
|`#cont [0 or 1 or 2]`|0: tied notes/rests retain phase. 1: ignore ties for phase reset/update. 2: as 1, but synchronized rests do not reset phase.|
|`#wcmd [0 or 1 or 2]`|Control related p/D/v/@v/( / )/V commands during waveform effects: 0 emit normally; 1 suppress output but remember values for effects; 2 ignore entirely.|
|`#compress [0 or 1]`|0 or omitted: rest compression; 1: also tied same-pitch notes. Initially off; explicit CLI/API settings take precedence.|
|`#opt ["dvqpt012*"]`|Select optimization; * means all categories, omitted argument stops optimization. Explicit CLI/API settings take precedence.|

For #save-tone/#save-wave, the last requested filename per type is used. The CLI saves relative names beside the declaring source file. The module returns bytes and leaves saving to its host. The restored -t/-w switches override these directives; their relative names resolve from the current working directory.

### Unsupported directives and PCM map

`#pcmlist [0 or 1]` is unsupported and emits a warning, but does not stop MDX conversion. No `pcmuse.map` is read, created or merged. The original 0/omitted mode generated a map (like -l); 1 OR-merged it with the old map (like -l -o). Those functions are outside the implemented scope. The console PCM report is independent of that file format. CLI -l and no-argument -o are recognized and warn rather than reporting Unknown option.

`#play`, `#remove`, `#beep` and `#ver` also warn and are ignored. Originally, #play ran a command after successful conversion unless Shift was held; #remove deleted an earlier output on error; #beep sounded on error; #ver selected verbose error display (without showing the current channel). These directives perform no host actions; the separately specified CLI -r/-b/-v switches now implement the corresponding functions described above. Other unsupported directives are errors.

### Defaults and repeated settings

The documented directive defaults are #tps 0, #detune 0, #natural "cdefgab", #list, #nreste 0, #ncoder, #glide 0, #cont 0, #wcmd 0 and #toneofs 0. Compression and optimization are initially disabled. Waveforms, voices and maps must be provided as required by their uses.

The original document explicitly allows repeated #title, #pcmfile, #include, #tps, #detune, #flat, #sharp, #natural/#normal, #octave-rev, #play, #nlist, #list, #reste, #nreste, #coder, #ncoder, #glide, #load-tone, #load-wave, #cont, #wcmd, #pcmlist, #compress, #save-tone, #save-wave, #opt, #ver and #toneofs. Unsupported directives remain ignored even when repeated.

## Notes and pitch

A note consists of a pitch followed by an optional duration. Omitted duration uses l. Pitches a–g accept + (sharp), - (flat), = (natural), and a double-quote character (natural). In quoted macro bodies, use = to avoid conflict with the string delimiter. Enharmonic forms are allowed: e+ is f, f- is e, b+ is the next c, and c- is the preceding b.

### Extension from the original: successive accidentals

notemdx.exe 0.8 processes every accidental after a pitch, in order. The first explicit accidental overrides the key signature; then each + raises a semitone, each - lowers a semitone, and = or a double quote restores the original natural pitch. Thus d++ is e (double sharp), d-- is c (double flat), d+++ is f, and d followed by + then a double quote is d natural. This also applies inside chords and to portamento targets. The generic sequential-natural rule is independent of any specific song.

The final playable MXDRV2 range is o0d+ through o8d (encoded 0–95). Octaves and numeric inputs may initially extend outside it if key-signature/transposition processing brings the final pitch into range. Key signatures apply first, then transposition.

Duration can be `%steps` or a denominator of a whole note. A quarter note is 48 steps; a whole note is 192. The denominator must divide 192 without a remainder: c4 is 48 steps. The original example uses a24 a24 a24 for three equal 8-step notes (total 24). A dot adds half the previous duration increment: 4. is 72 steps and 4.. is 84.

Use ^ to add and ~ to subtract lengths, mixing denominators and step values:

```
4. = 4^8 = %48^8 = 4^%24 = %48^%24
   = 2~8 = %96~8 = 2~%24 = %96~%24
```

The final duration must be 1–256 steps except for a tuplet's total length. The ^ and ~ operations may be combined.

## Numeric-note duration details

Usually write `n<pitch>,<length>`. A dot can replace the comma if followed by a number or % duration: n0.4 and n0.%48 both last 48 steps. Otherwise the dot augments the default duration, including when followed by ^.

|With l4|Steps|
|---|---|
|n0.|72|
|n0.^4|120|
|n0%48|48|
|n0,|48|
|n0^|96|
|n0~8|24|
|n0,.|72|
|n0^.|120|

## FM voice definitions

Define exactly 47 comma-separated values with `@<number> = { ... }`. The four operator rows each contain AR, DR, SR, RR, SL, TL, KS, MUL, DT1, DT2 and AME, followed by ALG, FL and OP. Voice numbers are 0–255. Ordinary voice definitions may appear anywhere; TL and automatic modulator selection require the referenced voice to be available when used.

```
@0 = {
/* AR  DR SR RR SL TL KS MUL DT1 DT2 AME
   31, 0, 3, 6, 0,28, 0, 1, 0, 0, 0,
   30, 0, 3, 6, 0,24, 0, 1, 0, 0, 0,
   28,18, 3, 9,11, 9, 0, 0, 0, 0, 0,
   26, 0,15,10, 0, 0, 0, 1, 0, 0, 0,
/* ALG FL OP
    3, 0,15
}
```

Indented continuation lines belong to the open definition and are not treated as whole-line comments.

## Macro definitions

Syntax: `<variable>[subscript] = "body"`. Permitted variable characters are I J K L M N O X Y Z, a–z, and half-width kana except ｡ ｰ ﾞ ﾟ ｢ ｣ ･ ､. The original has 91 variables, each with an unsubscripted form and subscripts 0–19: 1911 possible macro slots. Calls inside macros may nest to eight levels. Undefined macros expand to nothing. The original gives no per-body length limit; current overall source and expansion limits still apply. A quoted macro body ends at its matching closing quote; following explanatory text is not part of it.

## Tone macro definitions

Syntax: `@@<voice> = "body"`. Expand immediately after selecting that voice. SMON/SMOF enables/disables expansion; enabled by default. KS automatic voice selection does not invoke these macros. The current implementation rejects L/C and unmatched suppression boundaries inside tone macros; recursion depth and global expansion limits apply.

## Waveform effects in detail

Wave effects split notes and write ordinary MDX controls between the pieces; smaller element intervals can substantially increase the output size. They do not use a special waveform opcode in MDX.

For a synchronized, one-step, nonlooping pan wave, the original explanatory expansion is:

```
#wavemem
@w0 = {0,0,1,3}
A AP0,1,1 a%48

// Conceptually: A p1 a%1& p3 a%47
```

Mode 0 advances phase independently of note starts. Mode 1 resets phase at a note start. Mode 2 advances by note count instead of elapsed steps. Mode 2 normally ignores rests; #reste 1 makes rests count as notes. Tied notes normally share phase; #cont 1 treats each as a separate note for reset/update. #cont 2 additionally preserves synchronized phase across rests.

Positive delay postpones effect start. A negative delay is meaningful only in synchronized mode: the start position is key-off time plus the negative delay. If this is before the note start, use the note start instead. In a tied group this normally processes only the final note; #cont 1/2 applies the rule to each note separately. In other modes negative delay behaves as zero. The current compiler follows reference output that ignores positive delay in note-count mode.

## Wave memory definitions

Start at column 1: `@w<number> = {type,loop-point,data,...}`. Declare #wavemem first. Wave numbers are 0–127 and definitions take effect from their location onward. Type 0 is nonlooping; type 1 loops. The loop point is a zero-based index into the data portion, whose first element follows type and loop-point. There may be up to 512 data elements. Values are signed 16-bit (-32768…32767); $hex is also accepted. A nonlooping wave holds its final value. Redefining an active wave to a length that excludes its current position is rejected; turn the effect off first.

## Key-to-voice maps

Start at column 1: `@k<number> = {voice0,...,voice95}`. Exactly 96 voice numbers, each 0–255, correspond to o0d+ through o8d. Map numbers are 0–7. Definitions take effect from their location onward; select one with KS.

## Channel-specific content

```
ABC | D0p3v15c : D-4p1v13c : D4p2v12c |
```

Expands to A D0p3v15c, B D-4p1v13c and C D4p2v12c. Parts are assigned in ascending channel order, not in the order of written identifiers. Empty parts stay empty. BCD |:o4c:| and BCD |:o4c| both produce only C o4c. Extra parts are ignored. Keep the complete delimiter expression on one logical line.

## Chord input

Use half-width ｢｣ or paired backticks. `ABC ｢ceg｣8` (equivalently ABC followed by a backtick-delimited ceg and 8) expands to A c8, B e8 and C g8. Inside a chord, named notes, rests and < / > are recognized, so chords can cross octaves. The chord stores its composition; each channel's absolute octave is set outside it. Missing chord notes produce nothing by default; #coder pads with rests. Extra chord notes are ignored. `#[length]` reuses the stored composition. Keep chord delimiters on one logical line.

## Strings

For string-valued directives, an unquoted argument ends at the first space or tab: #title 1st. item sets the title to 1st. Quote whitespace with either single or double quotes. The other quote character may appear inside the quoted span. Adjacent quoted and unquoted pieces form one argument.

```
#title "It's"
#title It"'"s
#title '"start"'
#title '"'start'"'
```

The first pair produces It's; the second produces "start". Whitespace between directive name and argument is required: #title"start" is not a valid #title declaration. An unterminated quoted string extends to the end of its line. Thus #title "1st. 2nd., #title '1st. 2nd., #title 1st." 2nd. and #title 1st.' 2nd. all select 1st. 2nd. The special macro-body rule is described under Macro definitions.

## Key signatures

$FLAT/$SHARP/$NATURAL/$NORMAL affect the current channel; #flat/#sharp/#natural/#normal affect all channels. They have equal precedence: the later setting wins. Named PCM pitches are affected too. Apply the key signature before transposition; a note's explicit accidental overrides the signature as described under Notes and pitch.

## Glide details

The original examples illustrate conceptual expansion:

```
A GL-32,6 c4
// A D-32 c%6_D32 & D0 c%42
A GL-32,6 D4 c4
// A D-28 c%6_D32 & D4 c%42
A GL-64,6 o4 c4
// A o4 <b%6_>c & c%42
```

The ordinary D and #detune offsets are retained. An integral-semitone delta can use a shifted starting note rather than D if that pitch is in range. Reference output also requires detune-setting/restoring commands after GLON in some such cases; COMPATIBILITY.md and IL_VALIDATION.md record this behavior. Notes shorter than the glide interval are left unprocessed. The glide delta × 256 / steps must fit a signed 16-bit displacement or conversion fails.

## Relative volume

`v4 [a)]8` increments volume during each driver repeat, behaving like a sequence of v4, v5, v6 and so on, ending at v12 after eight increments. In contrast, `v4 [a V1]8` computes V1 when compiling the body and becomes `v4 [a v5]8`; it does not increment afresh at runtime. V uses the compiler's v/@v state and clips to its allowed range.

## Portamento limits

When a portamento slope exceeds the encoded range, the original behavior omits the slide. For example o0d+8_o8d becomes just o0d+8. If a longer target duration was specified, the remainder still plays at the target pitch: o0d+8_o8d4 becomes o0d+8 tied to o8d8. This is different from a valid slide that traverses continuously.

## TL waveform details

TL changes target operators relative to the total-level values in the selected reference voice. The definition must already exist, and a voice-selection command must have established the reference (or use TLT where appropriate). MXDRV applies voice data to OPM at key-on rather than immediately on @ selection. Consequently `@0 TL0,1,1 a` may not produce the expected first effect, while `@0 a TL0,1,1 a` has already keyed on once. This is a playback-driver constraint in the original document.

## Current limits and compatibility

The default complete-file budget is 64 KiB and may be increased with -mSIZE. Revision 2 reproduces the supplied 67,488-byte K01TAIL MDX exactly with -m256. It uses the ordinary format: voice data begins at body-relative offset 60550 and extends past the 64 KiB file boundary. Track and voice-data starting offsets must remain within 0–65535 relative to the body, independently of the complete-file budget. The original NOTE also rejected the six supplied tests with excessive track offsets or loop spans. See LARGE64K_VALIDATION.md and tests/large64k_reference for the evidence. Includes nest 16 levels; ordinary macros eight; repeats 32. Relative jumps are checked against signed 16-bit range. Exact threshold values have unit tests in this implementation but were not separately measured with the original NOTE. The module defaults to 8 MiB total source and 16 MiB expansion limits. Nested tuplets, waveform effects and loop-related commands inside tuplets, L within a finite repeat and L/C within tone macros are rejected. Some ties across intervening definitions/directives are not supported. Diagnostic columns count CP932 bytes, with tabs counting as one byte.

The bundled 11 real songs match the supplied MDX byte for byte using -c -z for STAGE5/STAGE6 and defaults for the other nine. Of 33 smaller references, 31 match verbatim; two EX-PCM references match only after adding their missing E8 declaration to comparison copies. Those two are not counted as raw matches. The two additional large-file references (56,093 and 67,488 bytes) and two binary banks also match. Six large-file tests are rejected by both converters; diagnostic text, position and count need not match. These checks do not establish complete compatibility for all inputs or verify playback on physical hardware. Windows VS2022 project files are supplied; the supplied revision 2 log confirms a successful MSVC build and seven passing CTest tests. Revision 3 fixes the remaining manual test encoding failure and is checked on Linux/GCC with a simulated CP932 default. A Windows rerun of revision 3 has not yet been confirmed.

## Origin and supplementary material

This tool was created from the documentation “mml file converter for mxdrv2 v0.8.5 1994,95 by DIS”, without using the original program's source code. The supplied note.doc is the basis for the input grammar. This is a documentation-based independent implementation; third-party implementation source code has not been used.

The following inventory matches COMPATIBILITY.md. The supplementary MDX-format document by YURAYSAN is reproduced at https://w.atwiki.jp/mxdrv/pages/23.html. Actual supplied output takes precedence where descriptions conflict.

|Material|Purpose and contents|Result and record|
|---|---|---|
|note.doc|Supplied NOTE v0.8.5 grammar and switches; unchanged CP932 original included|Basis of both manuals; current implementation notes take precedence|
|MXDRV data reference (YURAYSAN)|Supplementary output-format documentation at the URL above|Conflicts resolved using supplied output|
|priority.zip / effects.zip / followup.zip / remaining.zip (received 2026-09-25)|33 small reference MDX files and two binary banks|31 raw matches; two EX-PCM matches after correction of comparison copies; both banks match. VALIDATION.md|
|IL.zip (received 2026-09-25)|11 MUS/MDX song pairs|All match byte for byte; STAGE5/6 use -c -z, others use defaults. IL_VALIDATION.md|
|large64k.zip (received 2026-09-26)|Two original MDX files and eight logs; inputs are the eight probes/large64k MML files|K00SAFE (56,093 bytes) and K01TAIL (67,488 bytes) match with -m256. Both converters reject the other six inputs. LARGE64K_VALIDATION.md|
|err.log (received 2026-09-26)|User MSVC build and CTest log, separate from format evidence|Revision 2 built successfully and passed 7/8 tests. Revision 3 fixes the CP932 failure in the manual test. CTEST_FIX.md|

Across all 52 inputs, 44 match original MDX bytes, two match corrected EX-PCM comparison copies, and six reproduce rejection outcomes. The two binary banks are counted separately. This does not establish matching error text, positions or counts, or verify playback. The original NOTE logs do not record invocation arguments; requested commands are not proof of the arguments actually used. Documentation-based independent implementation includes these supplementary documents and output comparisons.

The unchanged original note.doc and user-provided music/reference data remain separate from the 0BSD license for newly written source code. See COMPATIBILITY.md, IL_VALIDATION.md and API.md for supporting details.
