// PLAN 9.19: depth of field (warden_dof.glsli), the blurred image over the sharp one at
// the display size - DAORecomp's dao_dof_combine.cs: blurred where the pixel's circle of
// confusion, or a near blur's reach over it, is more than a pixel or so. The blurred
// image is taken through a tent (four bilinear samples a half size pixel apart), which
// fills the gaps between the blur's samples.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "warden_dof.glsli"

// warden_dof_a: the upscaled scene; warden_dof_b: the blur.
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D warden_dof_out;

const float kDofSharp = 1.0;
const float kDofBlend = 2.0;

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, warden_dof.display_size))) {
    return;
  }
  vec4 sharp = texelFetch(warden_dof_a, ivec2(id), 0);
  float coc = DofCoc(id);
  if ((warden_dof.flags & kDofFlagShow) != 0u) {
    float shown = clamp(abs(coc) / max(warden_dof.max_radius, 1.0), 0.0, 1.0);
    vec3 tint = coc > 0.0 ? vec3(1.0, 0.25, 0.1) : vec3(0.1, 0.4, 1.0);
    vec3 gray = vec3(dot(DofLinear(sharp.rgb), vec3(0.2126, 0.7152, 0.0722)));
    imageStore(warden_dof_out, ivec2(id), vec4(DofEncode(mix(gray * 0.5, tint, shown)), 1.0));
    return;
  }
  vec2 texel = 1.0 / vec2(warden_dof.half_size);
  vec2 uv = (vec2(id) + 0.5) / vec2(warden_dof.display_size);
  vec4 blurred =
      (textureLod(sampler2D(warden_dof_b, warden_dof_linear), uv + vec2(-0.5, -0.5) * texel, 0.0) +
       textureLod(sampler2D(warden_dof_b, warden_dof_linear), uv + vec2(0.5, -0.5) * texel, 0.0) +
       textureLod(sampler2D(warden_dof_b, warden_dof_linear), uv + vec2(-0.5, 0.5) * texel, 0.0) +
       textureLod(sampler2D(warden_dof_b, warden_dof_linear), uv + vec2(0.5, 0.5) * texel, 0.0)) *
      0.25;
  // (The reach is in half size pixels.)
  float size = max(abs(coc), blurred.a * 2.0);
  float amount = clamp((size - kDofSharp) / kDofBlend, 0.0, 1.0);
  if (amount <= 0.0) {
    imageStore(warden_dof_out, ivec2(id), sharp);
    return;
  }
  vec3 color = mix(DofLinear(sharp.rgb), blurred.rgb, amount);
  imageStore(warden_dof_out, ivec2(id), vec4(DofEncode(color), sharp.a));
}
