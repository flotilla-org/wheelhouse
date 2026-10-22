// PROTOTYPE - throwaway (wheelhouse#431). Answers: which rendering API should
// the web terminal use, canvas 2D or WebGL2? Plays screen-state frames made
// from a real recording by Cleat's Ghostty engine (convert/), applies them to
// a cell model, and draws dirty rows once per animation frame. Both renderers
// read the same model; the bar switches between them and between real-time
// playback and as-fast-as-possible.
import { decodeMultiple } from "cbor-x";

// ---- model ------------------------------------------------------------------

type Style = { fg: number; bg: number; flags: number; ul: number; ulc: number | null; link: string | null };
const BOLD = 1, ITALIC = 2, FAINT = 4, INVERSE = 16, INVISIBLE = 32, STRIKE = 64;

class Model {
  cols = 0; rows = 0;
  text: (string | null)[] = [];
  style = new Uint32Array(0);
  styles: Style[] = [];
  dirty = new Set<number>();
  cursor = [0, 0, true, 0, false] as [number, number, boolean, number, boolean];
  alt = false;

  resize(cols: number, rows: number) {
    this.cols = cols; this.rows = rows;
    this.text = new Array(cols * rows).fill("");
    this.style = new Uint32Array(cols * rows);
    for (let r = 0; r < rows; r++) this.dirty.add(r);
  }

  apply(f: Map<number, any> | any) {
    const get = (k: number) => (f instanceof Map ? f.get(k) : f[k]);
    const size = get(1);
    if (size) this.resize(size[0], size[1]);
    for (const s of get(3) ?? []) this.styles[s[0]] = { fg: s[1], bg: s[2], flags: s[3], ul: s[4], ulc: s[5], link: s[6] };
    for (const [, rows] of get(4) ?? []) {
      for (const [row, , texts, runs] of rows) {
        const base = row * this.cols;
        for (let c = 0; c < this.cols; c++) this.text[base + c] = texts[c] ?? null;
        let c = 0;
        for (let i = 0; i < runs.length; i += 2) {
          this.style.fill(runs[i], base + c, base + c + runs[i + 1]);
          c += runs[i + 1];
        }
        this.dirty.add(row);
      }
    }
    if (get(2)) this.cursor = get(2);
    this.alt = !!get(5);
  }
}

// ---- metrics ----------------------------------------------------------------

const FONT_PX = 14, FONT = `"Menlo", "SF Mono", ui-monospace, monospace`;
const dpr = window.devicePixelRatio || 1;
function cellMetrics() {
  const c = document.createElement("canvas").getContext("2d")!;
  c.font = `${FONT_PX}px ${FONT}`;
  const m = c.measureText("M");
  return { w: m.width, h: Math.ceil(FONT_PX * 1.3), base: Math.round(FONT_PX * 1.3 * 0.78) };
}
const CELL = cellMetrics();
const hex = (n: number) => "#" + n.toString(16).padStart(6, "0");
function colours(s: Style): [number, number] {
  let fg = s.fg, bg = s.bg;
  if (s.flags & INVERSE) [fg, bg] = [bg, fg];
  if (s.flags & INVISIBLE) fg = bg;
  return [fg, bg];
}
const fontFor = (flags: number) => `${flags & ITALIC ? "italic " : ""}${flags & BOLD ? "bold " : ""}${FONT_PX}px ${FONT}`;

interface Renderer { canvas: HTMLCanvasElement; draw(m: Model, rows: Iterable<number>): void; sync(): void; destroy(): void }

// ---- canvas 2D --------------------------------------------------------------

class Canvas2D implements Renderer {
  canvas = document.createElement("canvas");
  ctx: CanvasRenderingContext2D;
  constructor(m: Model) {
    this.canvas.width = Math.ceil(m.cols * CELL.w * dpr); this.canvas.height = m.rows * CELL.h * dpr;
    this.canvas.style.width = `${this.canvas.width / dpr}px`; this.canvas.style.height = `${this.canvas.height / dpr}px`;
    this.ctx = this.canvas.getContext("2d", { alpha: false })!;
    this.ctx.scale(dpr, dpr);
    this.ctx.textBaseline = "alphabetic";
  }
  draw(m: Model, rows: Iterable<number>) {
    const { ctx } = this, W = CELL.w, H = CELL.h;
    for (const r of rows) {
      const base = r * m.cols, y = r * H;
      // Backgrounds, merged into runs.
      let c = 0;
      while (c < m.cols) {
        const bg = colours(m.styles[m.style[base + c]])[1];
        let e = c + 1;
        while (e < m.cols && colours(m.styles[m.style[base + e]])[1] === bg) e++;
        ctx.fillStyle = hex(bg);
        ctx.fillRect(Math.floor(c * W), y, Math.ceil((e - c) * W) + 1, H);
        c = e;
      }
      // Text: runs of one style and single-width ASCII are drawn as one string.
      c = 0;
      while (c < m.cols) {
        const t = m.text[base + c];
        if (!t || t === " ") { c++; continue; }
        const sid = m.style[base + c], s = m.styles[sid];
        const [fg] = colours(s);
        let str = t, e = c + 1;
        if (t.length === 1 && t.charCodeAt(0) < 128) {
          while (e < m.cols && m.style[base + e] === sid) {
            const u = m.text[base + e];
            if (!u || u.length !== 1 || u.charCodeAt(0) >= 128) break;
            str += u; e++;
          }
        }
        ctx.font = fontFor(s.flags);
        ctx.fillStyle = hex(fg);
        ctx.globalAlpha = s.flags & FAINT ? 0.55 : 1;
        ctx.fillText(str, c * W, y + CELL.base);
        if (s.ul) { ctx.fillRect(c * W, y + CELL.base + 2, (e - c) * W, 1); }
        if (s.flags & STRIKE) { ctx.fillRect(c * W, y + H / 2, (e - c) * W, 1); }
        ctx.globalAlpha = 1;
        c = e;
      }
    }
  }
  sync() { this.ctx.getImageData(0, 0, 1, 1); }
  destroy() {}
}

// ---- WebGL2 -----------------------------------------------------------------

const VS = `#version 300 es
uniform vec2 u_cell; uniform vec2 u_view; uniform int u_cols; uniform int u_pass; uniform vec2 u_slot; uniform float u_slots_per_row;
layout(location=0) in uvec4 a_cell; // glyph|wide<<20|colour<<21, fg, bg, flags|ul<<16
out vec2 v_uv; flat out uvec4 v_cell; out vec2 v_local;
void main() {
  int id = gl_InstanceID;
  vec2 cell = vec2(float(id % u_cols), float(id / u_cols));
  vec2 corner = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
  uint glyph = a_cell.x & 0xFFFFFu; bool wide = (a_cell.x & (1u << 20)) != 0u;
  float w = (u_pass == 1 && wide) ? 2.0 : 1.0;
  vec2 px = (cell + corner * vec2(w, 1.0)) * u_cell;
  if (u_pass == 1 && glyph == 0u && (a_cell.w & 0xFFFF0040u) == 0u) px = vec2(-10.0);
  gl_Position = vec4(px / u_view * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
  float g = float(glyph);
  vec2 slot = vec2(mod(g, u_slots_per_row), floor(g / u_slots_per_row));
  v_uv = (slot + corner * vec2(w * 0.5, 1.0)) * u_slot;
  v_local = corner * vec2(w, 1.0);
  v_cell = a_cell;
}`;
const FS = `#version 300 es
precision highp float; precision highp int;
uniform int u_pass; uniform sampler2D u_atlas;
in vec2 v_uv; flat in uvec4 v_cell; in vec2 v_local;
out vec4 o;
vec3 rgb(uint c) { return vec3(float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0; }
void main() {
  if (u_pass == 0) { o = vec4(rgb(v_cell.z), 1.0); return; }
  vec4 t = texture(u_atlas, v_uv);
  bool coloured = (v_cell.x & (1u << 21)) != 0u;
  uint flags = v_cell.w & 0xFFFFu;
  float faint = (flags & 4u) != 0u ? 0.55 : 1.0;
  float a = (v_cell.x & 0xFFFFFu) == 0u ? 0.0 : t.a;
  float y = fract(v_local.y);
  if ((v_cell.w >> 16) != 0u && y > 0.86 && y < 0.92) a = 1.0;
  if ((flags & 64u) != 0u && y > 0.48 && y < 0.54) a = 1.0;
  o = coloured && a == t.a ? t : vec4(rgb(v_cell.y), a * faint);
}`;

class WebGL implements Renderer {
  canvas = document.createElement("canvas");
  gl: WebGL2RenderingContext;
  prog: WebGLProgram; buf: WebGLBuffer; tex: WebGLTexture;
  data: Uint32Array;
  atlas = document.createElement("canvas");
  actx: CanvasRenderingContext2D;
  glyphs = new Map<string, number>([["", 0]]);
  slotW: number; slotH: number; perRow: number; size = 2048;
  constructor(m: Model) {
    this.canvas.width = Math.ceil(m.cols * CELL.w * dpr); this.canvas.height = m.rows * CELL.h * dpr;
    this.canvas.style.width = `${this.canvas.width / dpr}px`; this.canvas.style.height = `${this.canvas.height / dpr}px`;
    const gl = (this.gl = this.canvas.getContext("webgl2", { antialias: false, preserveDrawingBuffer: true })!);
    const sh = (t: number, s: string) => { const x = gl.createShader(t)!; gl.shaderSource(x, s); gl.compileShader(x); if (!gl.getShaderParameter(x, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(x)!); return x; };
    this.prog = gl.createProgram()!;
    gl.attachShader(this.prog, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(this.prog, sh(gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(this.prog);
    if (!gl.getProgramParameter(this.prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(this.prog)!);
    this.data = new Uint32Array(m.cols * m.rows * 4);
    this.buf = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buf);
    gl.bufferData(gl.ARRAY_BUFFER, this.data.byteLength, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribIPointer(0, 4, gl.UNSIGNED_INT, 16, 0);
    gl.vertexAttribDivisor(0, 1);
    this.slotW = Math.ceil(CELL.w * dpr); this.slotH = Math.ceil(CELL.h * dpr);
    this.perRow = Math.floor(this.size / (this.slotW * 2));
    this.atlas.width = this.slotW * 2; this.atlas.height = this.slotH;
    this.actx = this.atlas.getContext("2d", { willReadFrequently: true })!;
    this.tex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, this.size, this.size, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  }
  // Rasterise a glyph into the next atlas slot (two cells wide, for wide glyphs).
  glyph(text: string, flags: number): [number, boolean] {
    const key = (flags & 3) + text;
    let slot = this.glyphs.get(key);
    let coloured = false;
    if (slot === undefined) {
      slot = this.glyphs.size;
      const a = this.actx;
      a.clearRect(0, 0, this.atlas.width, this.atlas.height);
      a.font = fontFor(flags); a.fillStyle = "#fff"; a.textBaseline = "alphabetic";
      a.save(); a.scale(dpr, dpr); a.fillText(text, 0, CELL.base); a.restore();
      const px = a.getImageData(0, 0, this.atlas.width, this.atlas.height).data;
      for (let i = 0; i < px.length; i += 4) if (px[i + 3] && (px[i] !== px[i + 1] || px[i + 1] !== px[i + 2])) { coloured = true; break; }
      const x = (slot % this.perRow) * this.slotW * 2, y = Math.floor(slot / this.perRow) * this.slotH;
      const gl = this.gl;
      gl.bindTexture(gl.TEXTURE_2D, this.tex);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, x, y, gl.RGBA, gl.UNSIGNED_BYTE, this.atlas);
      this.glyphs.set(key, slot | (coloured ? 1 << 21 : 0));
      return [slot, coloured];
    }
    return [slot & 0xfffff, (slot & (1 << 21)) !== 0];
  }
  draw(m: Model, rows: Iterable<number>) {
    const gl = this.gl, d = this.data;
    let lo = Infinity, hi = -1;
    for (const r of rows) {
      lo = Math.min(lo, r); hi = Math.max(hi, r);
      for (let c = 0; c < m.cols; c++) {
        const i = r * m.cols + c, s = m.styles[m.style[i]], t = m.text[i];
        const [fg, bg] = colours(s);
        let g = 0, col = false;
        if (t && t !== " ") [g, col] = this.glyph(t, s.flags);
        const wide = c + 1 < m.cols && m.text[i + 1] === null;
        d[i * 4] = g | (wide ? 1 << 20 : 0) | (col ? 1 << 21 : 0);
        d[i * 4 + 1] = fg; d[i * 4 + 2] = bg; d[i * 4 + 3] = (s.flags & 0xffff) | (s.ul << 16);
      }
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buf);
    if (hi >= 0) gl.bufferSubData(gl.ARRAY_BUFFER, lo * m.cols * 16, d, lo * m.cols * 4, (hi - lo + 1) * m.cols * 4);
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.useProgram(this.prog);
    const u = (n: string) => gl.getUniformLocation(this.prog, n);
    gl.uniform2f(u("u_cell"), CELL.w * dpr, CELL.h * dpr);
    gl.uniform2f(u("u_view"), this.canvas.width, this.canvas.height);
    gl.uniform1i(u("u_cols"), m.cols);
    gl.uniform2f(u("u_slot"), this.slotW / this.size, this.slotH / this.size);
    gl.uniform1f(u("u_slots_per_row"), this.perRow);
    gl.uniform2f(u("u_slot"), (this.slotW * 2) / this.size, this.slotH / this.size);
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.disable(gl.BLEND);
    gl.uniform1i(u("u_pass"), 0);
    gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, m.cols * m.rows);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.uniform1i(u("u_pass"), 1);
    gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, m.cols * m.rows);
  }
  sync() { this.gl.readPixels(0, 0, 1, 1, this.gl.RGBA, this.gl.UNSIGNED_BYTE, new Uint8Array(4)); }
  destroy() { this.gl.getExtension("WEBGL_lose_context")?.loseContext(); }
}

// ---- player -----------------------------------------------------------------

const params = new URLSearchParams(location.search);
const kind = params.get("r") === "webgl" ? "webgl" : "canvas";
document.querySelectorAll<HTMLButtonElement>("[data-r]").forEach((b) => {
  b.classList.toggle("on", b.dataset.r === kind);
  b.onclick = () => { params.set("r", b.dataset.r!); location.search = params.toString(); };
});
const out = document.getElementById("m")!, term = document.getElementById("term")!, cursorEl = document.getElementById("cursor")!;
const stat = (xs: number[]) => {
  const s = [...xs].sort((a, b) => a - b);
  if (!s.length) return "-";
  const mean = s.reduce((a, b) => a + b, 0) / s.length;
  return `mean ${mean.toFixed(2)}  p95 ${s[Math.floor(s.length * 0.95)].toFixed(2)}  max ${s[s.length - 1].toFixed(2)} ms`;
};

const bytes = new Uint8Array(await (await fetch("/demo.cbor")).arrayBuffer());
let t0 = performance.now();
const frames = decodeMultiple(bytes) as any[];
const decodeMs = performance.now() - t0;
const header = `${kind}: ${frames.length} frames, ${(bytes.length / 1e6).toFixed(2)} MB CBOR, decode ${decodeMs.toFixed(1)} ms total (${(decodeMs / frames.length).toFixed(3)} ms/frame), dpr ${dpr}\n`;
out.textContent = header;

let model: Model, renderer: Renderer;
function fresh() {
  model = new Model();
  model.apply(frames[0] instanceof Map ? frames[0] : frames[0]);
  renderer?.destroy(); renderer?.canvas.remove();
  renderer = kind === "webgl" ? new WebGL(model) : new Canvas2D(model);
  term.prepend(renderer.canvas);
}
function placeCursor() {
  const [c, r, vis] = model.cursor;
  cursorEl.style.display = vis ? "block" : "none";
  cursorEl.style.left = `${c * CELL.w}px`; cursorEl.style.top = `${r * CELL.h}px`;
  cursorEl.style.width = `${CELL.w}px`; cursorEl.style.height = `${CELL.h}px`;
}
function present() {
  const rows = [...model.dirty]; model.dirty.clear();
  const t = performance.now();
  renderer.draw(model, rows);
  placeCursor();
  return [performance.now() - t, rows.length];
}

const results: Record<string, unknown> = ((window as any).__results = {});

async function realTime() {
  fresh();
  const apply: number[] = [], draw: number[] = [], gaps: number[] = [];
  let i = 1, start = performance.now(), last = start, presents = 0;
  await new Promise<void>((done) => {
    const tick = (now: number) => {
      gaps.push(now - last); last = now;
      const clock = now - start + frames[0][0];
      const t = performance.now();
      let any = false;
      while (i < frames.length && (frames[i].get?.(0) ?? frames[i][0]) <= clock) { model.apply(frames[i++]); any = true; }
      if (any) { apply.push(performance.now() - t); const [ms] = present(); draw.push(ms); presents++; }
      document.getElementById("pos")!.textContent = `frame ${i}/${frames.length}`;
      if (i < frames.length) requestAnimationFrame(tick); else done();
    };
    requestAnimationFrame(tick);
  });
  const late = gaps.filter((g) => g > 1000 / 60 * 1.5).length;
  results.real = { presents, apply: stat(apply), draw: stat(draw), late: `${late} of ${gaps.length} animation frames over 25 ms` };
  out.textContent = header + `real time: ${presents} presents\n  apply ${stat(apply)}\n  draw  ${stat(draw)}\n  ${results.real && (results.real as any).late}`;
}

async function maxSpeed() {
  fresh();
  const draw: number[] = [];
  const t = performance.now();
  for (let i = 1; i < frames.length; i++) {
    model.apply(frames[i]);
    const s = performance.now();
    present();
    renderer.sync();
    draw.push(performance.now() - s);
    if (i % 20 === 0) await new Promise((r) => setTimeout(r));
  }
  const total = performance.now() - t;
  results.max = { draw: stat(draw), total: total.toFixed(0), fps: (frames.length / total * 1000).toFixed(0) };
  out.textContent = header + `as fast as possible (draw includes a GPU/raster sync): ${frames.length} frames in ${total.toFixed(0)} ms = ${(frames.length / total * 1000).toFixed(0)} frames/s\n  draw ${stat(draw)}`;
}

document.getElementById("real")!.onclick = realTime;
document.getElementById("max")!.onclick = maxSpeed;
(window as any).__realTime = realTime;
(window as any).__maxSpeed = maxSpeed;
(window as any).__seek = (n: number) => { fresh(); for (let i = 1; i <= n && i < frames.length; i++) model.apply(frames[i]); present(); };
// N terminals playing the recording at staggered offsets, one renderer (and,
// for WebGL, one context) each, drawn in the same animation frame.
(window as any).__multi = async (n: number, seconds = 6) => {
  renderer.destroy(); renderer.canvas.remove();
  const box = document.getElementById("wrap")!;
  const terms: { m: Model; r: Renderer | null; i: number; off: number }[] = [];
  let failed = 0;
  for (let k = 0; k < n; k++) {
    const m = new Model(); m.apply(frames[0]);
    let r: Renderer | null = null;
    try { r = kind === "webgl" ? new WebGL(m) : new Canvas2D(m); r.canvas.style.width = "300px"; r.canvas.style.height = "auto"; box.append(r.canvas); } catch { failed++; }
    terms.push({ m, r, i: 1, off: (k * 397) % 6000 });
  }
  const draw: number[] = [], gaps: number[] = [];
  const start = performance.now(); let last = start;
  // Skip ahead to the animation, which redraws every row.
  const animStart = frames.findIndex((f) => (f[5] ?? f.get?.(5)) === true);
  for (const t of terms) { for (; t.i < animStart; t.i++) t.m.apply(frames[t.i]); t.m.dirty = new Set([...Array(t.m.rows).keys()]); }
  const t0 = frames[animStart][0] ?? frames[animStart].get(0);
  await new Promise<void>((done) => {
    const tick = (now: number) => {
      gaps.push(now - last); last = now;
      const s = performance.now();
      for (const t of terms) {
        const clock = t0 + ((now - start + t.off) % 5500);
        if (t.i >= frames.length || (frames[t.i][0] ?? frames[t.i].get(0)) > clock + 1000) { t.i = animStart; }
        while (t.i < frames.length && (frames[t.i][0] ?? frames[t.i].get(0)) <= clock) t.m.apply(frames[t.i++]);
        if (t.r && t.m.dirty.size) { const rows = [...t.m.dirty]; t.m.dirty.clear(); t.r.draw(t.m, rows); }
      }
      draw.push(performance.now() - s);
      if (now - start < seconds * 1000) requestAnimationFrame(tick); else done();
    };
    requestAnimationFrame(tick);
  });
  const late = gaps.filter((g) => g > 25).length;
  const contexts = kind === "webgl" ? terms.filter((t) => t.r && !(t.r as WebGL).gl.isContextLost()).length : n;
  return { n, failed, liveContexts: contexts, perFrame: stat(draw), late: `${late} of ${gaps.length} over 25 ms` };
};
(window as any).__times =() => frames.map((f) => f[0] ?? f.get?.(0));
fresh();
