// PLAN 9.19: depth of field (warden_dof.glsli), the blur at half size - DAORecomp's
// dao_dof_blur.cs: samples on a disc (a golden angle spiral, outwards), each counted
// where its own circle of confusion reaches this pixel, else as the average so far
// (after Dennis Gustafsson's single pass bokeh). A sample behind this pixel reaches it by
// at most twice this pixel's circle: a sharp face does not take the blurred wall's
// colour, while a blurred near shoulder spreads over it. Alpha: how far the samples in
// front of this pixel reach over it, for the combine.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "warden_dof.glsli"

// warden_dof_a: the half size scene.
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D warden_dof_blur_out;

const float kDofGoldenAngle = 2.39996323;

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, warden_dof.half_size))) {
    return;
  }
  vec2 texel = 1.0 / vec2(warden_dof.half_size);
  vec2 uv = (vec2(id) + 0.5) * texel;
  vec4 center = texelFetch(warden_dof_a, ivec2(id), 0);
  float center_size = abs(center.a);
  vec3 color = center.rgb;
  float reach = 0.0;
  float total = 1.0;
  // In half size pixels.
  float radius_max = warden_dof.max_radius * 0.5 * warden_dof.strength;
  float samples = float(warden_dof.samples);
  for (uint i = 0u; i < warden_dof.samples; ++i) {
    float radius = radius_max * sqrt((float(i) + 0.5) / samples);
    float angle = float(i) * kDofGoldenAngle;
    vec2 offset = vec2(cos(angle), sin(angle)) * radius;
    vec4 tap = textureLod(sampler2D(warden_dof_a, warden_dof_linear), uv + offset * texel, 0.0);
    float size = abs(tap.a);
    if (tap.a > center.a) {
      size = min(size, center_size * 2.0);
    }
    float counted = clamp(size - radius + 0.5, 0.0, 1.0);
    color += mix(color / total, tap.rgb, counted);
    reach += mix(reach / total, tap.a < center.a ? size : 0.0, counted);
    total += 1.0;
  }
  imageStore(warden_dof_blur_out, ivec2(id), vec4(color / total, reach / total));
}
