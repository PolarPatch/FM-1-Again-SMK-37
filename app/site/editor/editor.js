// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PolarPatch
// FM-1 Again's editor: the companion's parameters, live, and CHOP's sample (app/tools/again_chop.py's "AGC1": mono
// 22050 Hz, IMA ADPCM in blocks of 256 with each block's state, a 160-byte head with up to 32 slices and a name),
// made in the browser and sent as the companion's file "chop" (fm1-base's companion.js).
import { Companion, findCompanions } from "../companion/companion.js";

const $ = (id) => document.getElementById(id);
const RATE = 22050, BLOCK = 256, HEAD = 160, MAX_SLICES = 32;
const ROOM = 0x17000 - 32;                              // app.c CH_FILE (XDATA1), less plat_file's head
const MAX_N = Math.floor((ROOM - HEAD) / (4 + BLOCK / 2)) * BLOCK;
const IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
  6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767];
const IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

// ------------------------------------------------------------------ the FM-1
let dev = null, chopIndex = -1;
const state = (t) => { $("state").textContent = t; };
$("connect").addEventListener("click", async () => {
  try {
    if (!navigator.requestMIDIAccess) throw new Error("this browser has no Web MIDI: Chrome or Edge");
    state("LOOKING FOR THE FM-1…");
    const access = await navigator.requestMIDIAccess({ sysex: true });
    const found = await findCompanions(access);
    const f = found.find((x) => /again/i.test(x.hello.name)) || found[0];
    if (!f) throw new Error("no FM-1 with FM-1 Again answers: plugged in over USB?");
    if (dev) dev.close();
    dev = new Companion(f.input, f.output);
    state(`${f.hello.name} ${f.hello.appVersion || ""} ON ${f.name}`.toUpperCase());
    const files = await dev.files();
    chopIndex = (files.find((x) => x.name === "chop") || { index: -1 }).index;
    updateButtons();
    await loadParams();
  } catch (e) {
    state(String(e.message || e).toUpperCase());
  }
});

// ------------------------------------------------------------------ every value
const rows = new Map();                                  // id -> { input, out, p }
function show(r, v) {
  const p = r.p;
  r.input.value = String(v);
  r.out.textContent = p.type === "enum" ? "" : `${v}${p.unit ? " " + p.unit : ""}`;
}
async function loadParams() {
  const params = await dev.params(), box = $("groups");
  box.textContent = "";
  rows.clear();
  const groups = new Map();
  for (const p of params) {
    if (!groups.has(p.group)) {
      const g = document.createElement("div");
      g.className = "group";
      g.innerHTML = `<h3></h3>`;
      g.querySelector("h3").textContent = p.group.toUpperCase();
      box.appendChild(g);
      groups.set(p.group, g);
    }
    const row = document.createElement("div"), name = document.createElement("span"), out = document.createElement("output");
    row.className = "prm";
    name.textContent = p.name;
    let input;
    if (p.type === "enum") {
      input = document.createElement("select");
      p.labels.forEach((l, k) => { const o = document.createElement("option"); o.value = String(p.min + k); o.textContent = l; input.appendChild(o); });
    } else {
      input = document.createElement("input");
      input.type = "range";
      input.min = p.min; input.max = p.max; input.step = 1;
    }
    input.setAttribute("aria-label", `${p.group} ${p.name}`);
    const r = { input, out, p };
    input.addEventListener("input", async () => {
      const v = Number(input.value);
      try { show(r, await dev.set(p.id, v)); } catch (e) { state(String(e.message || e).toUpperCase()); }
    });
    row.append(name, input, out);
    groups.get(p.group).appendChild(row);
    rows.set(p.id, r);
  }
  const vals = await dev.get(params.map((p) => p.id));
  for (const [id, v] of vals) if (rows.has(id)) show(rows.get(id), v);
  dev.onchange = (id, v) => { if (rows.has(id)) show(rows.get(id), v); };
  await dev.watch(true);
  setInterval(() => dev && dev.watch(true).catch(() => {}), 5000);   // (the FM-1 stops watching after 10 s unasked)
}

// ------------------------------------------------------------------ the sample: in, shown, cut, heard
let x = null, cuts = [], rev = new Set();              // x: Float32Array at RATE; cuts: slice starts (0 first)
const ac = () => (window._ac ||= new AudioContext());
async function toRate(buf) {                             // any decoded sound -> mono at 22050 Hz, cut to fit
  const n = Math.min(Math.ceil(buf.duration * RATE), MAX_N);
  const off = new OfflineAudioContext(1, n, RATE), src = off.createBufferSource();
  src.buffer = buf;
  src.connect(off.destination);
  src.start();
  const out = (await off.startRendering()).getChannelData(0);
  let pk = 0;
  for (const v of out) pk = Math.max(pk, Math.abs(v));
  if (pk > 0) for (let i = 0; i < out.length; i++) out[i] *= 0.89 / pk;   // peaks at -1 dB
  return { y: out, cut: buf.duration * RATE > MAX_N };
}
async function take(buf, name) {
  const { y, cut } = await toRate(buf);
  x = y;
  if (name) $("name").value = name.replace(/\.[^.]*$/, "").toUpperCase().slice(0, 15);
  rev = new Set();
  cuts = onsets(x, 16);
  $("len").textContent = `${(x.length / RATE).toFixed(2)} S${cut ? " (CUT TO 8 S)" : ""}`;
  draw();
  updateButtons();
}
$("open").addEventListener("change", async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  try { await take(await ac().decodeAudioData(await f.arrayBuffer()), f.name); } catch (err) { state("THAT SOUND COULD NOT BE READ"); }
});
let recording = null;
$("rec").addEventListener("click", async () => {
  if (recording) { recording.stop(); return; }
  try {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
    const mr = new MediaRecorder(stream), parts = [];
    mr.ondataavailable = (e) => parts.push(e.data);
    mr.onstop = async () => {
      stream.getTracks().forEach((t) => t.stop());
      recording = null;
      $("rec").textContent = "● RECORD";
      clearTimeout(limit);
      const blob = new Blob(parts, { type: mr.mimeType });
      try { await take(await ac().decodeAudioData(await blob.arrayBuffer()), "RECORDING"); } catch { state("THE RECORDING COULD NOT BE READ"); }
    };
    mr.start();
    recording = mr;
    $("rec").textContent = "■ STOP";
    const limit = setTimeout(() => recording && recording.stop(), MAX_N / RATE * 1000);
  } catch { state("NO MICROPHONE (OR NOT ALLOWED)"); }
});

// where the sound starts again: the rise of its energy in 10 ms frames, the strongest, at least 150 ms apart
function onsets(y, most) {
  const f = 220, le = [];
  for (let i = 0; i + f < y.length; i += f) {
    let e = 1e-9;
    for (let j = i; j < i + f; j++) e += y[j] * y[j];
    le.push(Math.log10(e));
  }
  const top = Math.max(...le), flux = le.map((v, i) => (v < top - 2.5 ? 0 : Math.max(0, v - (le[i - 1] ?? v))));
  const cand = flux.map((v, i) => i).sort((a, b) => flux[b] - flux[a]), picked = [0];
  for (const i of cand) {
    if (flux[i] < 0.4 || picked.length >= most) break;
    if (picked.every((p) => Math.abs(i - p) * f >= RATE * 0.15)) picked.push(i);
  }
  return picked.map((p) => p * f).sort((a, b) => a - b);
}
$("even").addEventListener("click", () => { if (x) { cuts = [...Array(16).keys()].map((i) => Math.floor(i * x.length / 16)); rev.clear(); draw(); } });
$("auto").addEventListener("click", () => { if (x) { cuts = onsets(x, 16); rev.clear(); draw(); } });
$("clear").addEventListener("click", () => { if (x) { cuts = [0]; rev.clear(); draw(); } });

const cv = $("wave"), g = cv.getContext("2d");
function draw() {
  const w = cv.width, h = cv.height, mid = h / 2;
  g.fillStyle = "#c0c0c0"; g.fillRect(0, 0, w, h);
  if (!x) return;
  for (let c = 0; c < w; c++) {                          // peaks dark blue, RMS lighter, as the FM-1 draws them
    const a = Math.floor(c * x.length / w), b = Math.floor((c + 1) * x.length / w);
    let pk = 0, s = 0;
    for (let i = a; i < b; i++) { pk = Math.max(pk, Math.abs(x[i])); s += x[i] * x[i]; }
    const rms = Math.sqrt(s / Math.max(1, b - a));
    g.fillStyle = "#3232c8"; g.fillRect(c, mid - pk * mid, 1, Math.max(1, 2 * pk * mid));
    g.fillStyle = "#6464da"; g.fillRect(c, mid - rms * mid, 1, Math.max(1, 2 * rms * mid));
  }
  g.font = "bold 13px Helvetica, Arial, sans-serif";
  cuts.forEach((s, k) => {
    const cx = Math.round(s * w / x.length);
    g.fillStyle = "#181818"; g.fillRect(cx, 0, 2, h);
    g.fillText(`${rev.has(k) ? "<" : ""}${k + 1}`, cx + 4, 14);
  });
  const box = $("slices");
  box.textContent = "";
  cuts.forEach((_, k) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = `${rev.has(k) ? "<" : ""}${k + 1}`;
    b.className = rev.has(k) ? "rev" : "";
    b.title = "Play it (Shift: backwards). Alt-click: mark it to play backwards on the FM-1 too";
    b.addEventListener("click", (e) => {
      if (e.altKey) { rev.has(k) ? rev.delete(k) : rev.add(k); draw(); return; }
      hear(k, e.shiftKey || rev.has(k));
    });
    box.appendChild(b);
  });
}
function hear(k, back) {
  const a = cuts[k], b = k + 1 < cuts.length ? cuts[k + 1] : x.length, n = b - a;
  const buf = ac().createBuffer(1, n, RATE), d = buf.getChannelData(0);
  for (let i = 0; i < n; i++) d[i] = x[back ? b - 1 - i : a + i];
  const s = ac().createBufferSource();
  s.buffer = buf;
  s.connect(ac().destination);
  s.start();
}
// the cuts by hand: click adds, drag moves, double-click takes away (never the first: the sample's start)
let drag = -1;
const at = (e) => { const r = cv.getBoundingClientRect(); return Math.round((e.clientX - r.left) / r.width * x.length); };
const near = (s) => cuts.findIndex((c, k) => k > 0 && Math.abs(c - s) < x.length / 150);
cv.addEventListener("pointerdown", (e) => {
  if (!x) return;
  const s = at(e), k = near(s);
  if (k > 0) { drag = k; cv.setPointerCapture(e.pointerId); return; }
  if (cuts.length < MAX_SLICES && s > 0 && s < x.length) { cuts = [...cuts, s].sort((a, b) => a - b); rev.clear(); draw(); }
});
cv.addEventListener("pointermove", (e) => {
  if (drag < 1) return;
  const s = Math.max(cuts[drag - 1] + 64, Math.min(at(e), (cuts[drag + 1] ?? x.length) - 64));
  cuts[drag] = s;
  draw();
});
cv.addEventListener("pointerup", () => { drag = -1; });
cv.addEventListener("dblclick", (e) => {
  if (!x) return;
  const k = near(at(e));
  if (k > 0) { cuts.splice(k, 1); rev.clear(); draw(); }
});

// ------------------------------------------------------------------ the sample's file (again_chop.py's make)
function encode(y) {                                    // IMA ADPCM: each block its state, then its nibbles
  const nb = Math.ceil(y.length / BLOCK), out = new Uint8Array(nb * (4 + BLOCK / 2));
  let pred = 0, idx = 0, o = 0;
  for (let b = 0; b < nb; b++) {
    out[o++] = pred & 255; out[o++] = (pred >> 8) & 255; out[o++] = idx; out[o++] = 0;
    let lo = 0;
    for (let i = 0; i < BLOCK; i++) {
      const j = b * BLOCK + i, v = j < y.length ? Math.max(-32768, Math.min(32767, Math.round(y[j] * 32767))) : 0;
      let step = IMA_STEP[idx], d = v - pred, code = d < 0 ? 8 : 0;
      d = Math.abs(d);
      let diff = step >> 3;
      if (d >= step) { code |= 4; d -= step; diff += step; }
      step >>= 1;
      if (d >= step) { code |= 2; d -= step; diff += step; }
      step >>= 1;
      if (d >= step) { code |= 1; diff += step; }
      pred = Math.max(-32768, Math.min(32767, code & 8 ? pred - diff : pred + diff));
      idx = Math.max(0, Math.min(88, idx + IMA_INDEX[code]));
      if (i & 1) out[o++] = lo | code << 4; else lo = code;
    }
  }
  return { blocks: out, nb };
}
function agc() {
  const { blocks, nb } = encode(x), file = new Uint8Array(HEAD + blocks.length), dv = new DataView(file.buffer);
  file.set([65, 71, 67, 49]);                            // "AGC1"
  dv.setUint32(4, RATE, true);
  dv.setUint32(8, x.length, true);
  dv.setUint16(12, nb, true);
  file[14] = cuts.length;
  cuts.forEach((s, k) => dv.setUint32(16 + 4 * k, s, true));
  const nm = ($("name").value || "SAMPLE").toUpperCase().slice(0, 15);
  for (let k = 0; k < nm.length; k++) file[144 + k] = nm.charCodeAt(k) < 128 ? nm.charCodeAt(k) : 63;
  file.set(blocks, HEAD);
  // XDATA1's rule (plat.h): never "AT" at a sector's 0xF06 (the file sits after plat_file's 32-byte head)
  for (let a = 0xF06 - 32; a + 1 < file.length; a += 0x1000) if (file[a] === 65 && file[a + 1] === 84) file[a + 1] ^= 1;
  return file;
}
function updateButtons() {
  $("send").disabled = !(x && dev && chopIndex >= 0);
  $("save").disabled = !x;
  $("factory").disabled = !(dev && chopIndex >= 0);
}
$("save").addEventListener("click", () => {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([agc()], { type: "application/octet-stream" }));
  a.download = `${$("name").value.trim().toLowerCase().replace(/[^a-z0-9]+/g, "-") || "sample"}.agc`;
  a.click();
});
$("send").addEventListener("click", async () => {
  const data = agc();
  $("send").disabled = true;
  try {
    state("SENDING…");
    await dev.write(chopIndex, data, (i, n) => { $("meter").style.width = `${Math.round(i * 100 / n)}%`; });
    state(`SENT: ${$("name").value.toUpperCase()}${rev.size ? " (SET REVERSE ON THE FM-1: PRESET ON CHOP)" : ""}`);
  } catch (e) { state(String(e.message || e).toUpperCase()); }
  $("meter").style.width = "0";
  updateButtons();
});
$("factory").addEventListener("click", async () => {
  try { await dev.remove(chopIndex); state("CHOP: THE FACTORY PHRASE AGAIN"); } catch (e) { state(String(e.message || e).toUpperCase()); }
});
draw();
