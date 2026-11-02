// PLAN 9.19: depth of field (warden_dof.glsli), the scene at half size - DAORecomp's
// dao_dof_prefilter.cs: each half size pixel the average of its 2x2 display pixels,
// linear, and in alpha the smallest of their signed circles of confusion (the most in
// front, or the least behind), in half size pixels: an edge of something in focus does
// not take the blur behind it.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "warden_dof.glsli"

// warden_dof_a: the upscaled scene.
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D warden_dof_half_out;

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, warden_dof.half_size))) {
    return;
  }
  vec3 color = vec3(0.0);
  float coc = warden_dof.max_radius;
  for (uint i = 0u; i < 4u; ++i) {
    uvec2 pixel = min(id * 2u + uvec2(i & 1u, i >> 1u), warden_dof.display_size - 1u);
    color += min(DofLinear(texelFetch(warden_dof_a, ivec2(pixel), 0).rgb), vec3(kDofMaxColor));
    coc = min(coc, DofCoc(pixel));
  }
  imageStore(warden_dof_half_out, ivec2(id), vec4(color * 0.25, coc * 0.5));
}
