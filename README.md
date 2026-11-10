# Loops Master

All-in-one mastering for riddim producers: bass layers, EQ, multiband, kick duck, clip, true-peak
limit and loudness meters in one VST3 for Ableton Live on Windows and Mac.

## Download

Get the Windows or Mac zip from the [Releases](../../releases) page. Each zip has a
`READ ME FIRST.txt` with the install steps.

## How to use it in Ableton Live

1. In Live, open Settings > Plug-Ins, turn on **Use VST3 Plug-In System Folders**, and press **Rescan**.
2. Find **Loops Master** under Plug-Ins and drop it on an audio track.
3. Drop `sample/LoopsMaster_Demo_Bass_120bpm.wav` on that track and press play.
4. Open the plugin's window with the wrench icon on the device.

## The window

The tabs at the top switch between the Layers page and the Master page.

On the Layers page, the bands run from shallow to deep: Body, Sub, then Deep. Each one shows its layer as a live wave.
All three share one time scale and one zoom, so you can compare them directly. On a steady note,
the wave period doubles from Body to Sub and again from Sub to Deep. A flat line means that layer
is silent, usually because its Level is at 0%.

The top bar shows the note being followed.

- Drag a knob up or down to change it. Hold Shift for fine moves.
- Scroll over a knob to nudge it.
- Double-click a knob to reset it.

A dimmed control cannot change the sound right now, and you can still set it in advance. Sub and
Deep controls dim at 0% Level, Duck controls at 0% Depth, and Clip and Limit controls when their
switch is off. Width and Mono below dim on a mono signal, which has no stereo content to change.

## Controls

| Control | What it does |
|---|---|
| Preset | Init, Warm Lift, Sub Layer, Two Floors, Dirty Body, Kick Weight, Sub Only change the Layers page only. Master Safe, Master Loud, Master Slam, Bass Group, Drum Bus set both pages |
| Crossover, Slope | Where the low band is split off (40-320 Hz) and how steeply (12/24/48 dB/oct) |
| Body Level, Tone, Drive | The low band itself: its level, brightness and saturation |
| Sub Level, Tone, Drive | A new layer one octave below the note |
| Deep Level, Tone, Drive | A new layer two octaves below the note |
| On/Off (Body, Sub, Deep) | Switches a layer without losing its settings |
| Solo | Listen to the bass section alone |
| Input | Gain into the plugin (+/-24 dB) |
| Dry bass, High | The original signal below and above the crossover. At 100% each, the input is untouched |
| Dry, Wet | All of the original signal, and the sum of the new layers |
| Wet cut | High-pass on the new layers, to clear out sub too low to hear |
| Layer Out | Level of the layered signal going into the Master page (-24 to +12 dB) |

To replace the lows instead of adding to them, set Dry bass to 0% and Body to 100%. At 0% Drive and
100% Tone that sums back flat, so anything you then change is what you hear. The Replace Lows and
Replace + Sub presets start there.

Low Tone values give a clean sub. High values give a buzzier octaver sound.
On low notes, Deep can fall below what most speakers can play.

## The EQ page

Five bands over a live spectrum of the output. Drag a numbered point to change its frequency and
gain, scroll to change its Q, and double-click to flatten it. Each band can be a bell, a low or high
shelf, or a low or high cut. A cut is active as soon as it's selected, so set its frequency first.
Presets leave the EQ alone.

Each band also has a stereo placement (L, R, Stereo, Mid or Side). Shapes are Bell, Low and High
shelf, Low and High cut (12, 24 or 48 dB/oct), Notch, Band pass, Tilt and All pass.

## The Multi page

| Control | What it does |
|---|---|
| Down | Downward compression (the same control as Squash on the Master page) |
| Up | Lifts quiet material toward each band's usual level, without raising the noise floor |
| Time | Scales attack and release together (25-400%) |
| Low split, High split | Where the three bands divide |
| Low, Mid, High | Level of each band (+/-12 dB) |

Low at, Mid at and High at move where each band starts working (+/-24 dB). Each band can run Both,
Up only, Down only or Off, and L, M or H under Action lets you hear one band alone.

The Action bars show the gain on each band right now, and the panel shows a minute of short-term
loudness with the integrated figure and loudness range.

## The Master page

The signal runs top to bottom, then out. Every stage starts neutral or switched off except Clip
and Limit, which only act on signal close to or above their limits.

| Stage | Control | What it does |
|---|---|---|
| Duck | Depth, Band | Lowers only the signal below Band (60-400 Hz) while the kick plays |
| Duck | Threshold, Release | How loud the kick must be, and how long the low end takes to come back. Raise Release if you hear a click |
| Shape | Width, Mono below | Stereo width (0-150%), and mono below a chosen frequency |
| Shape | Squash | Three-band downward compression, split at 88 Hz and 2.5 kHz |
| Shape | Low, High | Shelves at 80 Hz and 8 kHz (+/-12 dB) |
| Shape | Punch | Lifts or softens the start of each hit |
| Clip | Heat | Saturation before the clipper |
| Clip | Drive, Softness | How hard the signal hits the clipper, and from hard to soft clipping. Runs at 4x oversampling |
| Clip | Clean below | Below this frequency, the signal skips the drive and distortion |
| Limit | Push, Ceiling, Release | True-peak limiter: gain into it, the final peak level, and recovery time |

Clip hard first and let the limiter do little: watch the Clip and Limit meters. Clean below works
best on a bass group. Close to the fundamental it can create peaks the limiter has to catch, which
is why Master Slam leaves it off. With Limit off, nothing holds the output to a ceiling.

The meters show level, gain reduction per stage, loudness (momentary, short term and integrated,
per ITU-R BS.1770), true peak and stereo correlation. Click the panel to restart the integrated
and true-peak readings.

### Ducking to a kick

Ducking needs the kick on a sidechain input, so it belongs on a bass group rather than on the
master, where the kick is already mixed in. Put Loops Master on the bass group, choose Bass Group,
and send the kick track to the plugin's Sidechain input. The Duck band says when the kick arrives.

### Clip and Limit additions

Clip's Ceiling sets the level peaks stop at, without makeup gain. Limit's style buttons replace
Release with a preset recovery. Unity takes Push back off so you can compare at matched level,
Audition plays only what the limiter is removing, DC filters out offset, and dither is TPDF with no
noise shaping. Leave dither off unless this is the last plugin before a 16 or 24 bit export.

### Ducking to the beat

Set Duck's source to 1/2, 1/4 or 1/8 and it ducks the low band on those beats of the host's tempo,
returning over Release. It needs no sidechain, and only ducks while the transport is playing.

### Latency

Every path is delayed by the same amount, about 3.3 ms at 44.1 kHz, and the amount is reported to
the host for compensation.

## Rebuilding

The release zips for both systems are built by the `release` workflow under Actions.

On a Mac with Xcode and CMake, `bash tools/build-mac.sh` builds and zips the Mac version.

On Windows it needs Visual Studio 2022 Build Tools, CMake, and the Steinberg VST3 SDK
(v3.8.1_build_84) cloned into `external/vst3sdk`.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

```bash
cmake --build build --config Release --target LoopsMaster dsp_test ui_test
```

`build/bin/Release/dsp_test.exe` runs the audio tests. `ui_test.exe` opens the plugin's window with
audio playing, sends it mouse input, and saves screenshots.

## The look

The window is drawn in code: white on black, with the Loops logo in the header. On the meters, levels
rise from the bottom in white, and gain being taken away hangs from the top in grey.
