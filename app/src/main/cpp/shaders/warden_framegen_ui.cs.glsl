// PLAN 9.14: frame generation without the UI in it (warden_frame_generation.cc). lsfg-vk
// is given the image before the UI - 9.4's capture, the scene after the post-processing's
// last pass - with the game's gamma ramp applied as Xenia's swap applies it (mode 0), and
// the UI of the real frame is put over the frame it generates (mode 1), found as the
// upscaler's composite finds it: where the front buffer differs from the image before the
// UI, at the render size (warden_upscale_composite.cs.glsl), the guest output - the real
// frame as shown, the ramp applied - instead. So the HUD and the floating names are never
// interpolated, and so never warp or ghost. With FSR 3.1's frame generation (mode 2) the
// frame is its frame interpolation's, from the upscaled scene before the UI: the ramp and
// the real frame's UI put on it in one pass.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Mode 0: the image before the UI at the guest output's size (the render size, or FSR
// 3.1's upscaled scene). Mode 1: unused. Mode 2: FSR 3.1's interpolated frame, before the
// ramp, at the guest output's size.
layout(set = 0, binding = 0) uniform texture2D warden_hudless;
// The front buffer and the image before the UI at the render size: where they differ is
// the UI.
layout(set = 0, binding = 1) uniform texture2D warden_front_render;
layout(set = 0, binding = 2) uniform texture2D warden_hudless_render;
// The guest output: the real frame with its UI and the ramp, at the guest output's size.
layout(set = 0, binding = 3) uniform texture2D warden_guest_output;
// Mode 0: lsfg-vk's next source. Mode 1: the generated frame, made over in place. Mode 2:
// the generated frame.
layout(set = 0, binding = 4, rgba8) uniform image2D warden_dest;
// The game's 256-entry gamma ramp: blue in bits 0:9, green in 10:19, red in 20:29.
layout(set = 0, binding = 5, std430) readonly buffer WardenGammaRamp {
  uint entries[256];
} warden_ramp;

layout(push_constant) uniform WardenFramegenUiConstants {
  uvec2 dest_size;
  vec2 render_size;
  uint mode;
} warden_ui;

float UiDifference(ivec2 p) {
  p = clamp(p, ivec2(0), ivec2(warden_ui.render_size) - 1);
  vec3 d = abs(texelFetch(warden_front_render, p, 0).rgb -
               texelFetch(warden_hudless_render, p, 0).rgb);
  return max(d.r, max(d.g, d.b));
}

vec3 ApplyRamp(vec3 color) {
  // UNORM conversion according to the Direct3D 10+ rules, as Xenia's apply_gamma_table.
  uvec3 index = uvec3(clamp(color, 0.0, 1.0) * 255.0 + 0.5);
  return vec3(float((warden_ramp.entries[index.r] >> 20) & 1023u),
              float((warden_ramp.entries[index.g] >> 10) & 1023u),
              float(warden_ramp.entries[index.b] & 1023u)) / 1023.0;
}

// The real frame's scene before its UI at the destination's size, before the ramp: in
// mode 1 the image the generator was given (binding 0, the guest output's size); in mode 2
// the render-size one, the destination pixel's render pixel.
vec3 SceneAtDest(ivec2 id) {
  if (warden_ui.mode == 1u) {
    return texelFetch(warden_hudless, id, 0).rgb;
  }
  vec2 uv = (vec2(id) + 0.5) / vec2(warden_ui.dest_size);
  ivec2 p = clamp(ivec2(uv * warden_ui.render_size), ivec2(0),
                  ivec2(warden_ui.render_size) - 1);
  return texelFetch(warden_hudless_render, p, 0).rgb;
}

// The real frame's UI over the generated scene: the generated frame's pixel, the real
// frame's, the real frame's scene before its UI (all after the ramp), the UI's opacity.
vec3 Over(vec3 generated, vec3 real, vec3 scene, float ui) {
  const vec3 kLuminance = vec3(0.299, 0.587, 0.114);
  float scene_luminance = dot(scene, kLuminance);
  float factor = clamp(dot(real, kLuminance) / max(scene_luminance, 1.0 / 255.0), 0.0, 1.0);
  vec3 off = abs(real - scene * factor);
  if (scene_luminance > 0.02 && max(off.r, max(off.g, off.b)) < 2.5 / 255.0) {
    return generated * factor;
  }
  return clamp(real + (generated - scene) * (1.0 - ui), 0.0, 1.0);
}

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, warden_ui.dest_size))) {
    return;
  }
  if (warden_ui.mode == 0u) {
    imageStore(warden_dest, ivec2(id),
               vec4(ApplyRamp(texelFetch(warden_hudless, ivec2(id), 0).rgb), 1.0));
    return;
  }
  // The render pixels this pixel covers - four where the guest output is larger (FSR
  // 3.1's), as the composite reads them.
  float difference;
  if (all(equal(vec2(warden_ui.dest_size), warden_ui.render_size))) {
    difference = UiDifference(ivec2(id));
  } else {
    vec2 uv = (vec2(id) + 0.5) / vec2(warden_ui.dest_size);
    ivec2 p = ivec2(floor(uv * warden_ui.render_size - 0.5));
    difference = max(max(UiDifference(p), UiDifference(p + ivec2(1, 0))),
                     max(UiDifference(p + ivec2(0, 1)), UiDifference(p + ivec2(1, 1))));
  }
  // BUG-113 (the owner, 2026-11-03: judder in the upper corners): the difference taken as
  // the UI's opacity, not as a yes or no. Any difference over 2/255 used to paste the real
  // frame's pixel whole, so the soft glows and rims around the portraits and the minimap
  // - a wide ring of faint UI over the scene - carried the real frame's scene with them,
  // and that ring stepped at the game's rate against the interpolated picture around it.
  // A pixel of UI with opacity a over a scene h is front = h (1 - a) + u a; over the
  // generated scene g it should be g (1 - a) + u a = front + (g - h) (1 - a), which is the
  // real frame's pixel where the UI is opaque and the generated scene plus the UI's light
  // where it is faint. The opacity is estimated from the difference, full at a quarter of
  // the range (the HUD's own colours stand well off the scene).
  // And a darkening that is the scene times one grey factor - the game's vignette in
  // the corners, drawn after the capture - is a shade, not UI: the factor goes on the
  // generated scene (warden_upscale_composite.cs.glsl's Shade), so the corners do not
  // step at the game's rate.
  float ui = clamp(difference * 4.0, 0.0, 1.0);
  if (warden_ui.mode == 2u) {
    vec3 generated = ApplyRamp(texelFetch(warden_hudless, ivec2(id), 0).rgb);
    vec3 real = texelFetch(warden_guest_output, ivec2(id), 0).rgb;
    vec3 scene = ApplyRamp(SceneAtDest(ivec2(id)));
    imageStore(warden_dest, ivec2(id), vec4(Over(generated, real, scene, ui), 1.0));
    return;
  }
  if (ui > 0.0) {
    vec3 generated = imageLoad(warden_dest, ivec2(id)).rgb;
    vec3 real = texelFetch(warden_guest_output, ivec2(id), 0).rgb;
    // The real frame's scene before its UI, as the generator was given it: the same
    // pixels, the ramp applied.
    vec3 scene = ApplyRamp(SceneAtDest(ivec2(id)));
    imageStore(warden_dest, ivec2(id), vec4(Over(generated, real, scene, ui), 1.0));
  }
}
