// PLAN 9.18: motion blur, first pass - DAORecomp's dao_motion_blur_tile_max.cs.hlsl. One
// workgroup per tile of the motion vectors: each pixel's blur length and log depth,
// together for the blur (warden_upscale_composite reads them), and the tile's longest blur
// vector. A tile with a camera cut in it gets none, and is counted. A pixel's blur is no
// longer than the motion where it was a frame ago: only motion that lasted is blurred,
// not one frame's wrong motion or motion every other frame, which would flicker. Blur
// vectors are half the motion over the shutter time, in render pixels, at most a tile.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Render pixels, from this frame to the one before (warden_upscale_motion).
layout(set = 0, binding = 0) uniform texture2D warden_motion;
layout(set = 0, binding = 1) uniform texture2D warden_depth;
// The motion of the frame before, and this frame's copy of it for the next.
layout(set = 0, binding = 2) uniform texture2D warden_previous_motion;
layout(set = 0, binding = 3, rg16f) uniform writeonly image2D warden_tiles;
// x: the blur vector's length, y: the log view depth.
layout(set = 0, binding = 4, rg16f) uniform writeonly image2D warden_pixels;
// Tiles with a cut, counted in [counter].
layout(set = 0, binding = 5, r32ui) uniform coherent uimage2D warden_cuts;
layout(set = 0, binding = 6, rg16f) uniform writeonly image2D warden_motion_copy;

layout(push_constant) uniform WardenBlurConstants {
  uvec2 size;  // the motion vectors'
  uint tile;   // render pixels per tile
  float scale;  // from motion to blur vectors
  // Motion longer than this in a frame (render pixels) is a camera cut, not motion.
  float cut;
  uint counter;
  uint cut_tiles;
  uint previous_valid;
} warden_blur;

shared vec4 warden_longest[64];

void main() {
  uvec2 group = gl_WorkGroupID.xy;
  uvec2 thread = gl_LocalInvocationID.xy;
  uint index = gl_LocalInvocationIndex;
  uvec2 origin = group * warden_blur.tile;
  float tile_size = float(warden_blur.tile);
  vec4 longest = vec4(0.0);
  for (uint y = thread.y; y < warden_blur.tile; y += 8u) {
    for (uint x = thread.x; x < warden_blur.tile; x += 8u) {
      uvec2 p = origin + uvec2(x, y);
      if (any(greaterThanEqual(p, warden_blur.size))) {
        continue;
      }
      vec2 motion = texelFetch(warden_motion, ivec2(p), 0).rg;
      imageStore(warden_motion_copy, ivec2(p), vec4(motion, 0.0, 0.0));
      vec2 blur = vec2(0.0);
      float motion_squared = dot(motion, motion);
      if (motion_squared > warden_blur.cut * warden_blur.cut) {
        longest.w = 1.0;
      } else {
        blur = motion * warden_blur.scale;
        // Where it was a frame ago (motion is the previous position minus this one);
        // outside the image, or with no motion before, none - so no blur.
        vec2 previous = vec2(0.0);
        ivec2 q = ivec2(floor(vec2(p) + 0.5 + motion));
        if (warden_blur.previous_valid != 0u && all(greaterThanEqual(q, ivec2(0))) &&
            all(lessThan(q, ivec2(warden_blur.size)))) {
          previous = texelFetch(warden_previous_motion, q, 0).rg;
        }
        float previous_squared = dot(previous, previous);
        if (previous_squared < motion_squared) {
          blur *= sqrt(previous_squared / motion_squared);
        }
        float length_squared = dot(blur, blur);
        if (length_squared > tile_size * tile_size) {
          blur *= tile_size * inversesqrt(length_squared);
        }
      }
      float blur_squared = dot(blur, blur);
      if (blur_squared > longest.z) {
        longest.xyz = vec3(blur, blur_squared);
      }
      // Log of the view depth, up to a constant (the depth is greater farther away).
      float depth = texelFetch(warden_depth, ivec2(p), 0).r;
      float log_depth = -log(max(1.0 - depth, 1.0e-7));
      imageStore(warden_pixels, ivec2(p), vec4(sqrt(blur_squared), log_depth, 0.0, 0.0));
    }
  }
  warden_longest[index] = longest;
  barrier();
  for (uint stride = 32u; stride != 0u; stride >>= 1) {
    if (index < stride) {
      vec4 other = warden_longest[index + stride];
      vec4 mine = warden_longest[index];
      if (other.z > mine.z) {
        mine.xyz = other.xyz;
      }
      mine.w = max(mine.w, other.w);
      warden_longest[index] = mine;
    }
    barrier();
  }
  if (index == 0u) {
    vec4 tile = warden_longest[0];
    if (tile.w != 0.0) {
      imageAtomicAdd(warden_cuts, ivec2(int(warden_blur.counter), 0), 1u);
    }
    imageStore(warden_tiles, ivec2(group), vec4(tile.w != 0.0 ? vec2(0.0) : tile.xy, 0.0, 0.0));
  }
}
