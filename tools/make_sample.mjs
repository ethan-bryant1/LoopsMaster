// Generates sample/LoopsMaster_Demo_Bass_120bpm.wav from scratch: a newly written
// four-bar bassline with a simple kick. No audio from any other product is used.
import { writeFileSync } from "node:fs";

const rate = 44100;
const bpm = 120;
const step = (60 / bpm) / 2; // eighth notes
const A1 = 55, C2 = 65.41, D2 = 73.42, E2 = 82.41, G1 = 49;
const line = [
  A1, 0, A1, A1, 0, A1, C2, 0,
  D2, 0, D2, 0, C2, A1, 0, A1,
  G1, 0, G1, G1, 0, G1, A1, 0,
  E2, 0, D2, 0, C2, 0, A1, 0,
];
const bars = 4;
const stepsPerBar = 8;
const totalSteps = bars * stepsPerBar;
const frames = Math.round(totalSteps * step * rate);
const buf = new Float64Array(frames);

function note(startSec, hz, seconds) {
  const start = Math.round(startSec * rate);
  const len = Math.round(seconds * rate);
  let phase = 0;
  let low = 0;
  for (let i = 0; i < len && start + i < frames; i++) {
    const t = i / rate;
    phase = (phase + hz / rate) % 1;
    const saw = 2 * phase - 1;
    const pulse = phase < 0.5 ? 1 : -1;
    const raw = 0.7 * saw + 0.3 * pulse;
    const cutoff = 180 + 1400 * Math.exp(-t * 14);
    low += (1 - Math.exp((-2 * Math.PI * cutoff) / rate)) * (raw - low);
    const attack = Math.min(1, i / (0.003 * rate));
    const release = Math.min(1, (len - i) / (0.012 * rate));
    buf[start + i] += 0.55 * low * attack * release * Math.exp(-t * 2.2);
  }
}

function kick(startSec) {
  const start = Math.round(startSec * rate);
  const len = Math.round(0.22 * rate);
  let phase = 0;
  for (let i = 0; i < len && start + i < frames; i++) {
    const t = i / rate;
    phase += (52 + 110 * Math.exp(-t * 38)) / rate;
    buf[start + i] += 0.5 * Math.sin(2 * Math.PI * phase) * Math.exp(-t * 15);
  }
}

line.forEach((hz, i) => {
  if (hz) note(i * step, hz, step * 0.92);
});
for (let beat = 0; beat < bars * 4; beat++) kick((beat * 60) / bpm);

let peak = 0;
for (const v of buf) peak = Math.max(peak, Math.abs(v));
const gain = 0.707 / peak;

const data = Buffer.alloc(frames * 4);
for (let i = 0; i < frames; i++) {
  const s = Math.round(buf[i] * gain * 32767);
  data.writeInt16LE(s, i * 4);
  data.writeInt16LE(s, i * 4 + 2);
}
const head = Buffer.alloc(44);
head.write("RIFF", 0);
head.writeUInt32LE(36 + data.length, 4);
head.write("WAVEfmt ", 8);
head.writeUInt32LE(16, 16);
head.writeUInt16LE(1, 20);
head.writeUInt16LE(2, 22);
head.writeUInt32LE(rate, 24);
head.writeUInt32LE(rate * 4, 28);
head.writeUInt16LE(4, 32);
head.writeUInt16LE(16, 34);
head.write("data", 36);
head.writeUInt32LE(data.length, 40);

const out = process.argv[2];
writeFileSync(out, Buffer.concat([head, data]));
console.log(`wrote ${out}: ${(frames / rate).toFixed(2)}s, ${rate} Hz, 16-bit stereo, peak -3 dBFS`);
