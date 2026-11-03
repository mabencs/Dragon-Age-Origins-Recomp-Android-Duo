// PLAN 9.4: the game's UI over FSR 3.1's upscaled scene - DAORecomp's
// dao_upscale_composite.hlsli, its SDR path. The UI is drawn after the image FSR 3.1
// receives, so it is found by comparing the final frame with that image: both come from
// the same render target, so outside the UI they are the same, and where they differ
// the final frame, scaled up bilinearly, is taken instead (small differences being faint
// UI - fades, shadows - blended in).
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D warden_upscaled;  // display resolution
layout(set = 0, binding = 1) uniform texture2D warden_final;     // render resolution, UI
layout(set = 0, binding = 2) uniform texture2D warden_hudless;   // render resolution
layout(set = 0, binding = 3) uniform sampler warden_linear;
layout(set = 0, binding = 4, rgba8) uniform writeonly image2D warden_dest;
// PLAN 9.18, motion blur: each render pixel's blur length and log depth, and the longest
// blur vector around each tile (warden_motion_blur_tile, warden_motion_blur_neighbor).
layout(set = 0, binding = 5) uniform texture2D warden_blur_pixels;
layout(set = 0, binding = 6) uniform texture2D warden_blur_around;

layout(push_constant) uniform WardenCompositeConstants {
  uvec2 dest_size;
  vec2 render_size;
  // Shows where the UI was found, in red over the scene in gray (a test switch).
  uint show_ui;
  // Motion blur: samples per pixel (0: none), render pixels per tile.
  uint blur_samples;
  uint blur_tile;
} warden_composite;

// PLAN 9.18: motion blur - DAORecomp's, the reconstruction filter of McGuire et al., "A
// Reconstruction Filter for Plausible Motion Blur" (I3D 2012). Each pixel gathers samples
// along the longest blur vector around its tile; a sample counts where it is in front
// and its own blur reaches this pixel, or behind and this pixel's blur reaches it, or
// both blur together.
const float kBlurMinimum = 0.5;    // render pixels: shorter is none
const float kBlurSoftDepth = 0.02;  // log view depth: how much nearer is in front
const float kBlurEdge = 0.05;       // where a blur reaches, fading either side
const float kBlurBothWeight = 2.0;  // the paper's

float InFront(float log_depth_a, float log_depth_b) {
  return clamp(1.0 - (log_depth_a - log_depth_b) / kBlurSoftDepth, 0.0, 1.0);
}

float Cone(float distance, float reach) {
  return clamp(1.0 - distance / reach, 0.0, 1.0);
}

float Cylinder(float distance, float reach) {
  return 1.0 - smoothstep((1.0 - kBlurEdge) * reach, (1.0 + kBlurEdge) * reach, distance);
}

// Jimenez's interleaved gradient noise: the samples offset per pixel, so few band less.
float Noise(uvec2 p) {
  return fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715))));
}

vec3 MotionBlur(uvec2 id, vec2 uv, vec3 center) {
  vec2 render_size = warden_composite.render_size;
  vec2 x = uv * render_size;
  ivec2 last = ivec2(render_size) - 1;
  ivec2 px = min(ivec2(x), last);
  vec2 around = texelFetch(warden_blur_around, px / int(warden_composite.blur_tile), 0).rg;
  // Most of a frame, still or slow, is not blurred.
  if (dot(around, around) < kBlurMinimum * kBlurMinimum) {
    return center;
  }
  vec2 pixel_x = texelFetch(warden_blur_pixels, px, 0).rg;
  float reach_x = max(pixel_x.x, kBlurMinimum);
  float depth_x = pixel_x.y;
  float weight = 1.0 / reach_x;
  vec3 sum = center * weight;
  float offset = Noise(id) - 0.5;
  uint samples = warden_composite.blur_samples;
  for (uint i = 0u; i < samples; ++i) {
    float t = mix(-1.0, 1.0, (float(i) + 1.0 + offset) / float(samples + 1u));
    vec2 y = x + around * t;
    vec2 pixel_y = texelFetch(warden_blur_pixels, clamp(ivec2(y), ivec2(0), last), 0).rg;
    float reach_y = max(pixel_y.x, kBlurMinimum);
    float depth_y = pixel_y.y;
    float distance = length(y - x);
    float sample_weight = InFront(depth_y, depth_x) * Cone(distance, reach_y) +
                          InFront(depth_x, depth_y) * Cone(distance, reach_x) +
                          Cylinder(distance, reach_y) * Cylinder(distance, reach_x) *
                              kBlurBothWeight;
    sum += sample_weight *
           textureLod(sampler2D(warden_upscaled, warden_linear), y / render_size, 0.0).rgb;
    weight += sample_weight;
  }
  return sum / weight;
}

// BUG-113 (the owner, 2026-11-03: judder in the upper corners): the game darkens the
// corners after the image FSR 3.1 receives - a vignette - and that darkening used to
// count as UI, so the corners showed the final frame at the render size, which moves by
// the jitter every frame. A darkening that is the scene times one grey factor (the
// vignette, a dark panel's shade) is a shade, not UI: the factor is put on the upscaled
// scene instead. The real frame's and the scene's pixels, the factor out.
bool Shade(vec3 real, vec3 scene, out float factor) {
  const vec3 kLuminance = vec3(0.299, 0.587, 0.114);
  float scene_luminance = dot(scene, kLuminance);
  factor = clamp(dot(real, kLuminance) / max(scene_luminance, 1.0 / 255.0), 0.0, 1.0);
  vec3 off = abs(real - scene * factor);
  return scene_luminance > 0.02 && max(off.r, max(off.g, off.b)) < 2.5 / 255.0;
}

float UiDifference(ivec2 p) {
  p = clamp(p, ivec2(0), ivec2(warden_composite.render_size) - 1);
  vec3 d = abs(texelFetch(warden_final, p, 0).rgb - texelFetch(warden_hudless, p, 0).rgb);
  return max(d.r, max(d.g, d.b));
}

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, warden_composite.dest_size))) {
    return;
  }
  vec2 uv = (vec2(id) + 0.5) / vec2(warden_composite.dest_size);
  // The four render pixels that bilinear filtering of this pixel reads (only this one
  // at the render size).
  float difference;
  if (all(equal(vec2(warden_composite.dest_size), warden_composite.render_size))) {
    difference = UiDifference(ivec2(id));
  } else {
    ivec2 p = ivec2(floor(uv * warden_composite.render_size - 0.5));
    difference = max(max(UiDifference(p), UiDifference(p + ivec2(1, 0))),
                     max(UiDifference(p + ivec2(0, 1)), UiDifference(p + ivec2(1, 1))));
  }
  float ui = clamp(difference * 128.0, 0.0, 1.0);
  vec3 color = texelFetch(warden_upscaled, ivec2(id), 0).rgb;
  // Not under opaque UI.
  if (warden_composite.blur_samples != 0u && ui < 1.0) {
    color = MotionBlur(id, uv, color);
  }
  float shade = 0.0;
  if (ui > 0.0) {
    vec3 final_color =
        textureLod(sampler2D(warden_final, warden_linear), uv, 0.0).rgb;
    vec3 scene_color =
        textureLod(sampler2D(warden_hudless, warden_linear), uv, 0.0).rgb;
    float factor;
    if (Shade(final_color, scene_color, factor)) {
      color *= factor;
      shade = 1.0;
    } else {
      color = mix(color, final_color, ui);
    }
  }
  if (warden_composite.show_ui != 0u) {
    // The UI red, a shade blue.
    float gray = dot(color, vec3(0.299, 0.587, 0.114)) * 0.5;
    color = mix(vec3(gray), mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.3, 1.0), shade), ui);
  }
  imageStore(warden_dest, ivec2(id), vec4(clamp(color, 0.0, 1.0), 1.0));
}
