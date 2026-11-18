// SPDX-License-Identifier: GPL-3.0-only
// KRIECHSTROM: the demo player. Every <audio data-peaks> (site/build.py) becomes a player: a button, the demo's
// waveform by bars (grey while dry, red once the feature is in, white where it is bright), a live scope and spectrum
// while it plays. Without this script, or with no Web Audio, the page keeps its plain players.
(function () {
  "use strict";
  var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  var RU = document.documentElement.lang === "ru";
  var ctx = null, players = [], current = null, raf = 0;

  function css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }

  function decode(str) {
    var n = Math.floor(str.length / 3), pk = new Float32Array(n), rms = new Float32Array(n), br = new Float32Array(n);
    for (var i = 0; i < n; i++) {
      pk[i] = B64.indexOf(str[3 * i]) / 63;
      rms[i] = B64.indexOf(str[3 * i + 1]) / 63;
      br[i] = B64.indexOf(str[3 * i + 2]) / 63;
    }
    return { pk: pk, rms: rms, br: br, n: n };
  }

  function audioCtx() {
    if (ctx) return ctx;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    try { ctx = new AC(); } catch (e) { ctx = null; }
    return ctx;
  }

  function Player(audio) {
    var self = this;
    this.audio = audio;
    this.name = audio.dataset.name || "";
    this.bars = +audio.dataset.bars || 8;
    this.dur = +audio.dataset.dur || 0;
    this.parts = (audio.dataset.parts || "").split("|").filter(Boolean).map(function (p) {
      var k = p.indexOf(":");
      return { bar: +p.slice(0, k), label: p.slice(k + 1) };
    });
    this.data = decode(audio.dataset.peaks);
    this.analyser = null;

    var box = document.createElement("div");
    box.className = "kp";
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "kp-play";
    btn.setAttribute("aria-label", (RU ? "Играть демо " : "Play the demo ") + this.name);
    btn.setAttribute("aria-pressed", "false");
    btn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path class="i-play" d="M7 4.5v15l12-7.5z"/>' +
      '<path class="i-pause" d="M6 4.5h4.5v15H6zM13.5 4.5H18v15h-4.5z"/></svg>';
    var body = document.createElement("div");
    body.className = "kp-body";
    var head = document.createElement("div");
    head.className = "kp-head";
    head.innerHTML = '<span class="kp-name"></span><span class="kp-part"></span><span class="kp-bar"></span>';
    head.querySelector(".kp-name").textContent = this.name;
    this.partEl = head.querySelector(".kp-part");
    this.barEl = head.querySelector(".kp-bar");
    var cv = document.createElement("canvas");
    cv.setAttribute("aria-hidden", "true");
    body.appendChild(head);
    body.appendChild(cv);
    box.appendChild(btn);
    box.appendChild(body);
    audio.parentNode.insertBefore(box, audio);
    box.appendChild(audio);
    audio.removeAttribute("controls");
    audio.hidden = true;
    this.box = box; this.btn = btn; this.cv = cv;

    btn.addEventListener("click", function () { self.toggle(); });
    btn.addEventListener("keydown", function (e) {
      var bar = self.duration() / self.bars;
      if (e.key === "ArrowRight") { self.seek(audio.currentTime + bar); e.preventDefault(); }
      if (e.key === "ArrowLeft") { self.seek(audio.currentTime - bar); e.preventDefault(); }
    });
    var drag = false;
    function at(e) {
      var r = cv.getBoundingClientRect();
      var x = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
      self.seek(x * self.duration());
    }
    cv.addEventListener("pointerdown", function (e) {
      drag = true; at(e);
      try { cv.setPointerCapture(e.pointerId); } catch (_) { }
      if (audio.paused) self.play();
    });
    cv.addEventListener("pointermove", function (e) { if (drag) at(e); });
    cv.addEventListener("pointerup", function () { drag = false; });
    audio.addEventListener("ended", function () { self.state(false); });
    audio.addEventListener("pause", function () { self.state(false); });
    audio.addEventListener("play", function () { self.state(true); });
    this.resize();
    this.draw();
  }

  Player.prototype.duration = function () {
    var d = this.audio.duration;
    return isFinite(d) && d > 0 ? d : this.dur;
  };

  Player.prototype.seek = function (t) {
    var d = this.duration();
    this.audio.currentTime = Math.max(0, Math.min(d - 0.01, t));
    this.draw();
  };

  Player.prototype.state = function (on) {
    this.box.classList.toggle("on", on);
    this.btn.setAttribute("aria-pressed", on ? "true" : "false");
    if (on) loop(); else this.draw();
  };

  Player.prototype.play = function () {
    var self = this;
    players.forEach(function (p) { if (p !== self && !p.audio.paused) p.audio.pause(); });
    current = this;
    var ac = audioCtx();
    if (ac && !this.analyser) {
      try {
        var src = ac.createMediaElementSource(this.audio);
        this.analyser = ac.createAnalyser();
        this.analyser.fftSize = 2048;
        this.analyser.smoothingTimeConstant = 0.7;
        src.connect(this.analyser);
        this.analyser.connect(ac.destination);
        this.wave = new Float32Array(this.analyser.fftSize);
        this.freq = new Uint8Array(this.analyser.frequencyBinCount);
      } catch (e) { this.analyser = null; }
    }
    if (ac && ac.state === "suspended") ac.resume();
    var p = this.audio.play();
    if (p && p.catch) p.catch(function () { self.state(false); });
  };

  Player.prototype.toggle = function () {
    if (this.audio.paused) this.play(); else this.audio.pause();
  };

  Player.prototype.resize = function () {
    var r = this.cv.getBoundingClientRect(), dpr = Math.min(2, window.devicePixelRatio || 1);
    this.w = Math.max(200, Math.round(r.width));
    this.h = Math.max(100, Math.round(r.height));
    this.cv.width = this.w * dpr;
    this.cv.height = this.h * dpr;
    this.dpr = dpr;
  };

  Player.prototype.partAt = function (bar) {
    var p = this.parts[0];
    for (var i = 0; i < this.parts.length; i++) if (bar >= this.parts[i].bar) p = this.parts[i];
    return p;
  };

  Player.prototype.draw = function () {
    var g = this.cv.getContext("2d"), w = this.w, h = this.h, d = this.data, self = this;
    var red = css("--red") || "#ff2018", line = css("--line") || "#3a1210", mid = css("--mid") || "#bdbdc4";
    var dur = this.duration(), t = this.audio.currentTime || 0, pos = dur ? t / dur : 0;
    var playing = !this.audio.paused;
    g.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    g.clearRect(0, 0, w, h);

    var top = Math.round(h * 0.42), ov = h - top - 14, mid0 = top + 14 + ov / 2;
    // the live part: a spectrum behind, a scope in front
    g.fillStyle = "rgba(255,32,24,0.04)";
    g.fillRect(0, 0, w, top - 4);
    if (playing && this.analyser) {
      this.analyser.getByteFrequencyData(this.freq);
      var nb = 64, bw = w / nb;
      for (var b = 0; b < nb; b++) {
        var lo = Math.floor(Math.pow(b / nb, 2) * 500) + 1, hi = Math.floor(Math.pow((b + 1) / nb, 2) * 500) + 2, m = 0;
        for (var k = lo; k < hi; k++) m = Math.max(m, this.freq[k]);
        var bh = (m / 255) * (top - 6);
        g.fillStyle = "rgba(255,32,24," + (0.12 + 0.35 * m / 255).toFixed(3) + ")";
        g.fillRect(b * bw + 1, top - 4 - bh, bw - 2, bh);
      }
      this.analyser.getFloatTimeDomainData(this.wave);
      var wv = this.wave, start = 0;
      for (var z = 1; z < wv.length / 2; z++) if (wv[z - 1] < 0 && wv[z] >= 0) { start = z; break; }
      g.beginPath();
      for (var x = 0; x < w; x++) {
        var v = wv[start + Math.floor(x / w * 1000)] || 0, y = (top - 4) / 2 - v * (top - 8) / 2 * 1.4;
        if (x) g.lineTo(x, y); else g.moveTo(x, y);
      }
      g.strokeStyle = "#fff";
      g.lineWidth = 1.5;
      g.shadowColor = red;
      g.shadowBlur = 8;
      g.stroke();
      g.shadowBlur = 0;
    } else {
      g.strokeStyle = line;
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(0, (top - 4) / 2 + 0.5);
      g.lineTo(w, (top - 4) / 2 + 0.5);
      g.stroke();
    }

    // the overview: bars, parts, the waveform
    var barW = w / this.bars;
    g.font = "600 10px 'IBM Plex Mono', ui-monospace, monospace";
    g.textBaseline = "top";
    for (var i = 0; i < this.bars; i++) {
      var bx = Math.round(i * barW) + 0.5;
      g.strokeStyle = line;
      g.beginPath(); g.moveTo(bx, top + 12); g.lineTo(bx, h); g.stroke();
    }
    this.parts.forEach(function (p, j) {
      var px = Math.round((p.bar - 1) * barW);
      var dry = j === 0 && self.parts.length > 1;
      g.fillStyle = dry ? mid : red;
      g.fillText(p.label, px + 4, top);
    });
    var cw = w / d.n;
    for (var c = 0; c < d.n; c++) {
      var cx = c * cw, f = (c + 0.5) / d.n, bar = Math.floor(f * this.bars) + 1;
      var dryCol = this.parts.length > 1 && this.partAt(bar) === this.parts[0];
      var done = f <= pos;
      var ph = d.pk[c] * ov / 2, rh = d.rms[c] * ov / 2, br = d.br[c];
      var base = dryCol ? [150, 150, 158] : [255, 32 + br * 120, 24 + br * 90];
      var a = done ? 1 : (playing || pos > 0 ? 0.35 : 0.6);
      g.fillStyle = "rgba(" + Math.round(base[0]) + "," + Math.round(base[1]) + "," + Math.round(base[2]) + "," + (a * 0.45).toFixed(3) + ")";
      g.fillRect(cx, mid0 - ph, Math.max(1, cw - 0.6), ph * 2);
      var wr = dryCol ? 0 : br * br;
      g.fillStyle = "rgba(" + Math.round(base[0] + (255 - base[0]) * wr) + "," + Math.round(base[1] + (255 - base[1]) * wr) + "," +
        Math.round(base[2] + (255 - base[2]) * wr) + "," + a.toFixed(3) + ")";
      g.fillRect(cx, mid0 - rh, Math.max(1, cw - 0.6), rh * 2);
    }
    if (pos > 0) {
      var hx = Math.round(pos * w) + 0.5;
      g.strokeStyle = "#fff";
      g.lineWidth = 1.5;
      g.beginPath(); g.moveTo(hx, top + 12); g.lineTo(hx, h); g.stroke();
    }

    var curBar = Math.min(this.bars, Math.floor(pos * this.bars) + 1);
    this.barEl.textContent = (RU ? "ТАКТ " : "BAR ") + curBar + "/" + this.bars;
    var part = this.partAt(curBar);
    var label = part ? part.label : "";
    this.partEl.textContent = (playing || pos > 0) && label !== this.name ? label : "";
    this.partEl.classList.toggle("fx", !!part && this.parts.indexOf(part) > 0);
  };

  function loop() {
    if (raf) return;
    (function tick() {
      var any = false;
      players.forEach(function (p) { if (!p.audio.paused) { p.draw(); any = true; } });
      raf = any ? requestAnimationFrame(tick) : 0;
    })();
  }

  function init() {
    var list = document.querySelectorAll("audio[data-peaks]");
    for (var i = 0; i < list.length; i++) players.push(new Player(list[i]));
    var rt = 0;
    window.addEventListener("resize", function () {
      clearTimeout(rt);
      rt = setTimeout(function () { players.forEach(function (p) { p.resize(); p.draw(); }); }, 100);
    });
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(function () { players.forEach(function (p) { p.draw(); }); });
    document.addEventListener("keydown", function (e) {
      if (e.key !== " " || !current) return;
      var tg = e.target.tagName;
      if (tg === "INPUT" || tg === "TEXTAREA" || tg === "BUTTON" || tg === "SELECT" || e.target.isContentEditable) return;
      e.preventDefault();
      current.toggle();
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init); else init();
})();
