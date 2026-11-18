// ps_2_0_002 WGSL translation
const WGSL_FS = `
struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) col0: vec4f,
  @location(1) col1: vec4f,
  @location(2) uv0: vec4f,
  @location(3) uv1: vec4f,
  @location(4) uv2: vec4f,
  @location(5) uv3: vec4f,
  @location(6) fog: f32,
};
struct PSPar { aref: f32, usec: f32, pad0: f32, pad1: f32, proj: vec4f, c: array<vec4f, 32>, fog: vec4f };
@group(0) @binding(2) var<uniform> P: PSPar;
override ALPHA_FUNC: u32 = 7u;
override PS20_TEXCOORD: bool = true;
fn alpha_pass(a: f32, r: f32) -> bool {
  switch ALPHA_FUNC {
    case 1u: { return false; }
    case 2u: { return a < r; }
    case 3u: { return a == r; }
    case 4u: { return a <= r; }
    case 5u: { return a > r; }
    case 6u: { return a != r; }
    case 7u: { return !(a < r); }
    default: { return true; }
  }
}
fn ps_end(c: vec4f, fog: f32) -> vec4f {
  if (ALPHA_FUNC != 8u) { if (!alpha_pass(c.a, P.aref)) { discard; } }
  return vec4f(mix(P.fog.rgb, c.rgb, mix(1.0, saturate(fog), P.fog.a)), c.a);
}
fn tcoord(q: vec4f, sel: f32) -> vec2f {
  let m = i32(sel) >> 4u;
  return q.xy / select(select(1.0, q.z, m == 1), q.w, m == 2);
}
struct PSOut { @location(0) col: vec4f, @builtin(frag_depth) depth: f32 };
@group(1) @binding(0) var tex0: texture_2d<f32>;
@group(1) @binding(1) var tex1: texture_2d<f32>;
@group(1) @binding(2) var tex2: texture_2d<f32>;
@group(1) @binding(3) var tex3: texture_2d<f32>;
@group(1) @binding(4) var smp0: sampler;
@group(1) @binding(5) var smp1: sampler;
@group(1) @binding(6) var smp2: sampler;
@group(1) @binding(7) var smp3: sampler;
@fragment fn ps_0(i: VOut) -> PSOut {
  let Z = vec4f(0.0);
  var r0 = vec4f(0.0);
  let c1 = vec4f(1.0, 0.0, 0.0, 0.0);
  var t0 = vec4f(0.0);
  var t1 = vec4f(0.0);
  var oC = vec4f(0.0);
  var depth = vec4f(0.0);
  t0 = select(vec4f(0.0), i.uv0, PS20_TEXCOORD);
  t1 = select(vec4f(0.0), i.uv1, PS20_TEXCOORD);
  r0 = textureSample(tex0, smp0, (t0).xy);
  r0 = (r0.wwww) + ((-P.c[0].xxxx));
  if (any(r0 < vec4f(0.0))) { discard; }
  { let e = vec4f(select(1.0 / (t1.wwww).x, 3.402823466e+38, (t1.wwww).x == 0.0)); r0.x = e.x; }
  { let e = (r0.xxxx) * (t1.zzzz); r0.x = e.x; }
  depth = r0.xxxx;
  r0 = c1.xxxx;
  oC = r0;
  return PSOut(oC, depth.x);
}
`;

const WGSL_VS = `
struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) col0: vec4f,
  @location(1) col1: vec4f,
  @location(2) uv0: vec4f,
  @location(3) uv1: vec4f,
  @location(4) uv2: vec4f,
  @location(5) uv3: vec4f,
  @location(6) fog: f32,
};
@vertex fn main(@builtin(vertex_index) vid: u32) -> VOut {
  var pos = array<vec4f, 6>(
    vec4f(-1.0, -1.0, 0.0, 1.0), vec4f(1.0, -1.0, 0.0, 1.0), vec4f(-1.0, 1.0, 0.0, 1.0),
    vec4f(-1.0, 1.0, 0.0, 1.0), vec4f(1.0, -1.0, 0.0, 1.0), vec4f(1.0, 1.0, 0.0, 1.0)
  );
  var uv0 = array<vec4f, 6>(
    vec4f(0.0, 1.0, 0.0, 1.0), vec4f(1.0, 1.0, 0.0, 1.0), vec4f(0.0, 0.0, 0.0, 1.0),
    vec4f(0.0, 0.0, 0.0, 1.0), vec4f(1.0, 1.0, 0.0, 1.0), vec4f(1.0, 0.0, 0.0, 1.0)
  );
  var uv1 = array<vec4f, 6>(
    vec4f(0.0, 1.0, 0.0, 1.0), vec4f(1.0, 1.0, 0.0, 1.0), vec4f(0.0, 0.0, 0.0, 1.0),
    vec4f(0.0, 0.0, 0.0, 1.0), vec4f(1.0, 1.0, 0.0, 1.0), vec4f(1.0, 0.0, 0.0, 1.0)
  );
  var col0 = array<vec4f, 6>(vec4f(1.0), vec4f(1.0), vec4f(1.0), vec4f(1.0), vec4f(1.0), vec4f(1.0));
  var col1 = array<vec4f, 6>(vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0));
  var uv2 = array<vec4f, 6>(vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0));
  var uv3 = array<vec4f, 6>(vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0), vec4f(0.0));
  var fog = array<f32, 6>(1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
  return VOut(pos[vid], col0[vid], col1[vid], uv0[vid], uv1[vid], uv2[vid], uv3[vid], fog[vid]);
}
`;