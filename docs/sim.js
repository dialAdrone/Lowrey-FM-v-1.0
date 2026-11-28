// Lowrey FM in the browser: lowrey.wasm is main.c with web_fm1.c (the FM-1's API).
(async () => {
  const $ = (id) => document.getElementById(id);
  const BTN = { OCT_DOWN: 0, OCT_UP: 1, FX: 2, SCL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
  const ENC = [["SELECT", 0, true], ["ALGORITHM", 1, true], ["PRESETS", 6, true], ["K1", 2], ["K2", 3], ["K3", 4], ["K4", 5]];
  let notes = 0, buttons = 0, ex = null, mem = null;

  // ---- the firmware
  const stub = new Proxy({}, { get: (_, name) => name === "random_get"
      ? (p, n) => { crypto.getRandomValues(new Uint8Array(mem.buffer, p, n)); return 0; } : () => 0 });
  const { instance } = await WebAssembly.instantiateStreaming(fetch("lowrey.wasm"), { wasi_snapshot_preview1: stub });
  ex = instance.exports; mem = ex.memory;
  if (ex._initialize) ex._initialize();
  try {                                          // the settings kept in this browser
    const s = localStorage.getItem("lowrey-store");
    if (s) { const b = Uint8Array.from(atob(s), (c) => c.charCodeAt(0)); new Uint8Array(mem.buffer, ex.sim_store(), ex.sim_store_size()).set(b.subarray(0, ex.sim_store_size())); }
  } catch (e) {}
  ex.sim_setup();
  const push = () => ex.sim_keys(notes >>> 0, buttons >>> 0);

  // ---- the screen and the lights
  const ctx = $("lcd").getContext("2d"), img = ctx.createImageData(240, 240), px = img.data;
  const btnEls = {}, keyEls = [];
  function draw() {
    const fb = new Uint16Array(mem.buffer, ex.sim_fb(), 240 * 240);
    for (let i = 0, j = 0; i < fb.length; i++, j += 4) {
      const q = fb[i]; px[j] = (q >> 11) << 3 | (q >> 13); px[j + 1] = ((q >> 5) & 63) << 2 | ((q >> 9) & 3); px[j + 2] = (q & 31) << 3 | ((q >> 2) & 7); px[j + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
    const led = new Uint8Array(mem.buffer, ex.sim_leds(), 42);
    for (const [id, el] of Object.entries(btnEls)) {
      let l = led[id]; if (l === 4) l = (Date.now() % 1100 < 550) ? 2 : 1;
      if (+id === BTN.PLAY && led[41] && !led[id]) l = 3;
      el.classList.toggle("l1", l === 1); el.classList.toggle("l2", l === 2); el.classList.toggle("l3", l === 3);
      el.classList.toggle("red", +id === BTN.REC || (+id === BTN.PLAY && led[41] > 0));
    }
    keyEls.forEach((el, n) => { const l = led[14 + n]; el.classList.toggle("lit", l >= 2); });
    requestAnimationFrame(draw);
  }

  // ---- buttons
  function makeBtn(parent, name, id) {
    const b = document.createElement("button"); b.type = "button"; b.textContent = name; parent.appendChild(b); btnEls[id] = b;
    const down = (e) => { e.preventDefault(); b.setPointerCapture?.(e.pointerId); buttons |= 1 << id; b.classList.add("down"); push(); };
    const up = () => { buttons &= ~(1 << id); b.classList.remove("down"); push(); };
    b.addEventListener("pointerdown", down); b.addEventListener("pointerup", up); b.addEventListener("pointercancel", up);
  }
  ["FX", "SCL", "HOME", "SAVE"].forEach((n) => makeBtn($("btns-top"), n, BTN[n]));
  ["ENV", "LFO", "EDIT", "GLO", "ARP", "SEQ", "PLAY", "REC"].forEach((n) => makeBtn($("btns-voice"), n, BTN[n]));
  makeBtn($("oct"), "OCT +", BTN.OCT_UP); makeBtn($("oct"), "OCT -", BTN.OCT_DOWN);

  // ---- knobs: drag or scroll; each step is one click
  for (const [name, e, big] of ENC) {
    const k = document.createElement("div"); k.className = "knob" + (big ? " big" : "");
    k.innerHTML = `<div class="dial"></div><div class="lbl">${name}</div>`; $(big ? "knobs" : "knobs2").appendChild(k);
    let ang = 0, y0 = null, acc = 0;
    const turn = (d) => { ex.sim_enc(e, d); ang += d * 15; k.style.setProperty("--a", ang + "deg"); };
    k.addEventListener("pointerdown", (ev) => { y0 = ev.clientY; acc = 0; k.setPointerCapture(ev.pointerId); });
    k.addEventListener("pointermove", (ev) => { if (y0 === null) return; acc += y0 - ev.clientY; y0 = ev.clientY;
      while (Math.abs(acc) >= 8) { turn(Math.sign(acc)); acc -= 8 * Math.sign(acc); } });
    k.addEventListener("pointerup", () => (y0 = null)); k.addEventListener("pointercancel", () => (y0 = null));
    k.addEventListener("wheel", (ev) => { ev.preventDefault(); turn(ev.deltaY < 0 ? 1 : -1); }, { passive: false });
  }
  $("master").addEventListener("input", (ev) => ex.sim_master(+ev.target.value));

  // ---- the keyboard: 27 keys, F3 to G5
  const kb = $("kb"), BLACK = [1, 3, 5, 8, 10], NAMES = ["F", "", "G", "", "A", "", "B", "C", "", "D", "", "E"];
  let wx = 0; const WW = 100 / 16;
  for (let n = 0; n < 27; n++) {
    const s = n % 12, isB = BLACK.includes(s), el = document.createElement("div");
    el.className = "k " + (isB ? "b" : "w");
    if (isB) { el.style.left = `calc(${wx * WW}% - ${WW * 0.32}%)`; el.style.width = WW * 0.64 + "%"; }
    else { el.style.left = wx * WW + "%"; el.style.width = WW + "%"; el.innerHTML = `<span>${NAMES[s]}</span>`; wx++; }
    kb.appendChild(el); keyEls[n] = el;
  }
  const held = new Map();                       // pointer -> key
  const setKey = (n, on) => { if (n == null) return; notes = on ? notes | (1 << n) : notes & ~(1 << n); keyEls[n].classList.toggle("on", on); push(); };
  const keyAt = (x, y) => { const el = document.elementFromPoint(x, y); const n = keyEls.indexOf(el?.closest?.(".k")); return n < 0 ? null : n; };
  kb.addEventListener("pointerdown", (e) => { e.preventDefault(); kb.setPointerCapture(e.pointerId); const n = keyAt(e.clientX, e.clientY); held.set(e.pointerId, n); setKey(n, true); });
  kb.addEventListener("pointermove", (e) => { if (!held.has(e.pointerId)) return; const n = keyAt(e.clientX, e.clientY), o = held.get(e.pointerId);
    if (n !== o) { setKey(o, false); setKey(n, true); held.set(e.pointerId, n); } });
  const lift = (e) => { if (!held.has(e.pointerId)) return; setKey(held.get(e.pointerId), false); held.delete(e.pointerId); };
  kb.addEventListener("pointerup", lift); kb.addEventListener("pointercancel", lift);
  // the home row plays the white keys from F, the row above the black ones, as on a piano
  const MAP = { a: 0, w: 1, s: 2, e: 3, d: 4, r: 5, f: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12, o: 13, l: 14, p: 15, ";": 16, "[": 17, "'": 18 };
  addEventListener("keydown", (e) => { if (e.repeat || e.metaKey || e.ctrlKey) return; const n = MAP[e.key.toLowerCase()]; if (n !== undefined) setKey(n, true); });
  addEventListener("keyup", (e) => { const n = MAP[e.key.toLowerCase()]; if (n !== undefined) setKey(n, false); });

  // ---- MIDI in (Chrome, Edge)
  navigator.requestMIDIAccess?.().then((m) => { const hook = () => m.inputs.forEach((i) => (i.onmidimessage = (ev) => { const [s, a = 0, b = 0] = ev.data; if (s < 0xF0) ex.sim_midi(s, a, b); })); hook(); m.onstatechange = hook; }).catch(() => {});

  // ---- power on: the audio drives the firmware, as on the FM-1
  $("go").addEventListener("click", () => {
    const ac = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 44100 });
    const sp = ac.createScriptProcessor(1024, 0, 2);
    sp.onaudioprocess = (ev) => {
      const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
      for (let o = 0; o < L.length; o += 256) {
        const b = new Float32Array(mem.buffer, ex.sim_block(), 512);
        for (let i = 0; i < 256; i++) { L[o + i] = b[2 * i]; R[o + i] = b[2 * i + 1]; }
      }
    };
    sp.connect(ac.destination); ac.resume();
    $("start").remove(); requestAnimationFrame(draw);
  });
  setInterval(() => { try { if (ex.sim_store_dirty()) { const b = new Uint8Array(mem.buffer, ex.sim_store(), ex.sim_store_size());
    let s = ""; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); localStorage.setItem("lowrey-store", btoa(s)); } } catch (e) {} }, 2000);
  draw();
})();
