// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PolarPatch
// FM-1 Again's front page: the glitching titles, and the emulator (fm1-base's emu/, in a frame of this site) dressed
// in black and white and played by the demos: they press its buttons, turn its knobs and play its keys as a hand
// would (the emulator's own messages to its audio worklet: "buttons", "enc", "keys").

import "./glitch.js";

const $ = (id) => document.getElementById(id);
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

// ------------------------------------------------------------------ the hero's waveform, and the cards' waves
{
  const b = $("bars");
  let v = 0.4;
  for (let i = 0; i < 120; i++) {
    v = Math.min(1, Math.max(0.08, v + (Math.random() - 0.5) * 0.35));
    const kick = i % 8 === 0 ? 1 : v;
    const e = document.createElement("i");
    e.style.height = `${Math.round(kick * 100)}%`;
    b.appendChild(e);
  }
}
document.querySelectorAll(".card svg").forEach((svg, i) => {   // three copies 70 apart: an endless wave that rolls
  const p = svg.querySelector("path"), g = document.createElementNS("http://www.w3.org/2000/svg", "g");
  g.setAttribute("class", "roll");
  g.style.setProperty("--spd", (0.9 + (i * 0.37) % 1.2).toFixed(2) + "s");
  for (const dx of [-70, 0, 70]) {
    const c = p.cloneNode();
    c.setAttribute("transform", `translate(${dx} 0)`);
    g.appendChild(c);
  }
  p.replaceWith(g);
});

// ------------------------------------------------------------------ the emulator, in black and white
const frame = $("emu");
const STYLE = `
:root:root:root { --ground: #000 !important; --ink: #fff !important; --muted: #9a9a9a !important; --rule: #222 !important;
  --accent: #fff !important; --teal: #7a7ae6 !important; --card: #0a0a0a !important; --focus: #fff !important;
  --bad: #ff8a8a !important; --panel-lit: #fff; --panel-lit-ink: #000; --panel-lit-white: #fff; --panel-lit-black: #7a7ae6;
  --panel-dim: #555; --panel-dim-white: #e6e6e6; --panel-dim-black: #9a9ae0; color-scheme: dark; }
body { padding: 0 !important; background: #000 !important; }
body > header, .turn, #session, #reset, .help { display: none !important; }
main { max-width: none !important; }
.start { border-radius: 0 !important; background: rgba(0, 0, 0, .55) !important; }
.start button { border-radius: 0 !important; background: #fff !important; color: #000 !important;
  font: 700 1.6rem "Helvetica Neue", Helvetica, Arial, sans-serif !important; letter-spacing: .02em !important; }
.held .btnface, .held .keyface { stroke: #fff !important; }
.latched .btnface, .latched .keyface { stroke: #7a7ae6 !important; }
.bar button, .speak button { border-radius: 0 !important; }
.speak { border-radius: 0 !important; }`;
const W = () => frame.contentWindow;
function fit() {
  try { frame.style.height = `${frame.contentDocument.documentElement.scrollHeight}px`; } catch { /* not loaded */ }
}
frame.addEventListener("load", () => {
  const d = frame.contentDocument, s = d.createElement("style");
  s.textContent = STYLE;
  d.head.appendChild(s);
  fit();
  new ResizeObserver(fit).observe(d.body);
});
addEventListener("resize", fit);

// the emulator's own functions and its state (its page's script: switchOn, send; its last frame, emuLast)
async function on() {
  const w = W();
  if (w.document.getElementById("start").hidden) return;
  await w.switchOn();
  if (!w.document.getElementById("start").hidden) throw new Error("not switched on");
}
const send = (m) => W().send(m);
const BIT = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11 };
const ENC = { SELECT: 0, ALGO: 1, PRESET: 2, K1: 3, K2: 4, K3: 5, K4: 6 };
const PAGE = { HOME: 0, GROOVE: 1, RISE: 2, BUILD: 3, KEYS: 4, VOX: 5, CHOP: 6, PUMP: 7, SPACE: 8, ECHO: 9, SETUP: 10, TAPE: 11, MIX: 12 };
const playing = () => { const m = W().emuLast; return !!(m && m.buttons >> BIT.PLAY & 1); };
async function tap(b) { send({ type: "buttons", mask: 1 << BIT[b] }); await wait(90); send({ type: "buttons", mask: 0 }); await wait(90); }
async function turn(k, n) { send({ type: "enc", role: ENC[k], n }); await wait(60); }
async function set(k, v) { await turn(k, -64); await turn(k, v); }      // (the values stop at their ends: from 0)
async function page(p) { await set("SELECT", PAGE[p]); }
async function keys(list, ms) {                                           // a key at a time
  for (const [k, len] of list) {
    send({ type: "keys", mask: 1 << k });
    await wait(len || ms);
    send({ type: "keys", mask: 0 });
    await wait(40);
  }
}
async function loop() { await page("BUILD"); await set("K4", 0); }   // PLAY: LOOP (not SONG)
async function stop() { if (playing()) await tap("PLAY"); }
async function start() { if (!playing()) await tap("PLAY"); }

// ------------------------------------------------------------------ the demos
const GROOVES = ["HOUSE", "2-STEP", "GARAGE 4X4", "SHUFFLE", "HALFTIME", "BROKEN", "MINIMAL", "KICK ONLY"];
const DEMOS = [
  { id: "A1", name: "NIGHT BUS", sub: "2-step, the chords pumping under the kick", groove: 1,
    run: async () => { await stop(); await loop(); await page("KEYS"); await set("K3", 2); await page("HOME"); await set("PRESET", 1);
                       await set("ALGO", 0); await set("K1", 70); await start(); } },
  { id: "A2", name: "LONGING", sub: "house, a voice sung in pieces on the keys", groove: 0,
    run: async () => { await stop(); await loop(); await page("KEYS"); await set("K3", 1); await page("HOME"); await set("PRESET", 0);
                       await set("ALGO", 1); await start(); await wait(1900);
                       await keys([[16, 380], [14, 300], [11, 520], [9, 260], [14, 300], [7, 420], [11, 600]]); } },
  { id: "A3", name: "BUILD · DROP", sub: "four bars of stutter, then the drop", groove: null,
    run: async () => { await page("BUILD"); await set("K1", 1); await set("K2", 2); await page("HOME");
                       if (!playing()) { await set("PRESET", 1); await start(); await wait(1500); }
                       await tap("ARP"); } },
  { id: "A4", name: "SONG", sub: "a whole track, intro to outro, by itself", groove: 2,
    run: async () => { await stop(); await page("BUILD"); await set("K4", 1); await page("HOME"); await set("PRESET", 2);
                       await start(); } },
  { id: "A5", name: "CHOP REVERSE", sub: "a slice forwards, then backwards", groove: null,
    run: async () => { await set("ALGO", 2); await page("CHOP"); await set("K4", 0); await keys([[0, 120]]);
                       await turn("PRESET", -1); await keys([[0, 700]]); await wait(300);
                       await turn("PRESET", 1); await wait(300); await keys([[0, 700]]); } },
  { id: "A6", name: "FLOAT · MEMORY", sub: "halftime on a worn tape", groove: 4,
    run: async () => { await stop(); await loop(); await page("TAPE"); await set("PRESET", 4); await page("KEYS"); await set("K3", 4);
                       await page("HOME"); await set("PRESET", 4); await set("ALGO", 0); await start(); } },
];
let busy = false;
async function play(d, btn) {
  if (busy) return;
  busy = true;
  try {
    await on();
    await wait(300);
    document.querySelectorAll(".demo").forEach((b) => b.classList.toggle("on", b === btn));
    $("r-demo").textContent = d.name;
    if (d.groove !== null) $("r-groove").textContent = GROOVES[d.groove];
    await d.run();
  } catch { $("r-demo").textContent = "SWITCH ON FIRST"; }
  busy = false;
}
for (const d of DEMOS) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "demo";
  for (const [cls, text] of [["id", d.id], ["n", d.name], ["s", d.sub]]) {
    const s = document.createElement("span");
    s.className = cls;
    s.textContent = text;
    b.appendChild(s);
  }
  b.addEventListener("click", () => play(d, b));
  $("demos").appendChild(b);
}
$("d-stop").addEventListener("click", () => { try { stop(); } catch { /* off */ } });
$("d-off").addEventListener("click", () => { try { send({ type: "keys", mask: 0 }); send({ type: "buttons", mask: 0 }); } catch { /* off */ } });
$("hero-go").addEventListener("click", () => {
  $("play").scrollIntoView({ block: "start" });
  on().catch(() => {});
});
setInterval(() => {                                       // PLAYING, from the PLAY button's light
  try { $("r-play").textContent = playing() ? "PLAY" : "STOP"; } catch { /* not loaded */ }
}, 250);
