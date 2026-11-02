// PLAN 9.19: depth of field's focus (warden_dof.glsli) - DAORecomp's dao_dof_focus.cs.
// One workgroup: the scene's distances on a 16x16 grid over most of the screen's width,
// from under the top to past the middle (where the game's conversation shots frame the
// faces), sorted; the focus is the nearer quarter's, so the people in front of a far
// wall are in focus. Eased towards that over frames, or taken at once on a cut: the
// area's median distance changing by a ratio of more than kDofCut either way.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "warden_dof.glsli"

// x 0: the focus, x 1: the median.
layout(set = 0, binding = 4, r32f) uniform coherent image2D warden_dof_focus_out;

const vec2 kDofAreaMin = vec2(0.15, 0.18);
const vec2 kDofAreaMax = vec2(0.85, 0.6);
// Beyond this (near-plane units) a sample is the sky: not a focus.
const float kDofSky = 1.0e5;
const float kDofCut = 1.5;

shared float warden_dof_samples[256];

void main() {
  uvec2 id = gl_LocalInvocationID.xy;
  uint index = gl_LocalInvocationIndex;
  vec2 position = mix(kDofAreaMin, kDofAreaMax, (vec2(id) + 0.5) / 16.0);
  uvec2 render_pixel = min(uvec2(position * vec2(warden_dof.render_size)),
                           warden_dof.render_size - 1u);
  warden_dof_samples[index] =
      min(DofDistance(texelFetch(warden_dof_depth, ivec2(render_pixel), 0).r), 1.0e7);
  barrier();
  // Bitonic sort, ascending.
  for (uint size = 2u; size <= 256u; size <<= 1u) {
    for (uint stride = size >> 1u; stride > 0u; stride >>= 1u) {
      uint partner = index ^ stride;
      if (partner > index) {
        float a = warden_dof_samples[index];
        float b = warden_dof_samples[partner];
        bool ascending = (index & size) == 0u;
        if ((a > b) == ascending) {
          warden_dof_samples[index] = b;
          warden_dof_samples[partner] = a;
        }
      }
      barrier();
    }
  }
  if (index != 0u) {
    return;
  }
  float target = warden_dof_samples[64];
  float median = warden_dof_samples[128];
  vec2 previous = vec2(imageLoad(warden_dof_focus_out, ivec2(0, 0)).r,
                       imageLoad(warden_dof_focus_out, ivec2(1, 0)).r);
  bool snap = (warden_dof.flags & kDofFlagSnap) != 0u ||
              !(previous.x > 0.0 && previous.x < 1.0e8) ||
              !(previous.y > 0.0 && previous.y < 1.0e8) ||
              max(median / previous.y, previous.y / median) > kDofCut;
  if (target > kDofSky) {
    // Sky in the middle: keep the focus (or, with none yet, far).
    target = snap ? kDofSky : previous.x;
  }
  // Eased in log distance: a pull from 2 to 4 as smooth as from 20 to 40.
  float focus = snap ? target : exp(mix(log(previous.x), log(target), warden_dof.focus_rate));
  imageStore(warden_dof_focus_out, ivec2(0, 0), vec4(focus));
  imageStore(warden_dof_focus_out, ivec2(1, 0), vec4(median));
}
