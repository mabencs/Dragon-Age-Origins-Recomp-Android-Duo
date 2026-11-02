// PLAN 9.18: motion blur, second pass - DAORecomp's dao_motion_blur_neighbor_max.cs.hlsl.
// For each tile, the longest blur vector of the tile and its eight neighbours (from
// warden_motion_blur_tile), so a pixel knows how far blur from outside its tile reaches
// it. None at all when enough tiles had a camera cut: then the whole frame is one (a
// conversation's shot changing), and parts of it moving less than the cut would streak.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D warden_tiles;
layout(set = 0, binding = 1, rg16f) uniform writeonly image2D warden_around;
layout(set = 0, binding = 2, r32ui) uniform coherent uimage2D warden_cuts;

layout(push_constant) uniform WardenBlurConstants {
  uvec2 size;  // the tile counts
  uint tile;
  float scale;
  float cut;
  uint counter;
  uint cut_tiles;
  uint previous_valid;
} warden_blur;

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (all(equal(id, uvec2(0)))) {
    // The other counter, for the next frame.
    imageStore(warden_cuts, ivec2(int(warden_blur.counter ^ 1u), 0), uvec4(0u));
  }
  if (any(greaterThanEqual(id, warden_blur.size))) {
    return;
  }
  vec2 longest = vec2(0.0);
  if (imageLoad(warden_cuts, ivec2(int(warden_blur.counter), 0)).r < warden_blur.cut_tiles) {
    float longest_squared = 0.0;
    for (int y = -1; y <= 1; ++y) {
      for (int x = -1; x <= 1; ++x) {
        ivec2 p = clamp(ivec2(id) + ivec2(x, y), ivec2(0), ivec2(warden_blur.size) - 1);
        vec2 blur = texelFetch(warden_tiles, p, 0).rg;
        float blur_squared = dot(blur, blur);
        if (blur_squared > longest_squared) {
          longest = blur;
          longest_squared = blur_squared;
        }
      }
    }
  }
  imageStore(warden_around, ivec2(id), vec4(longest, 0.0, 0.0));
}
