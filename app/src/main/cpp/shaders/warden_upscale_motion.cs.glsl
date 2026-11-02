// PLAN 9.4: FSR 3.1's motion vectors from the camera's motion (warden_upscale.cc). Each
// pixel's place in the frame before, from the scene's depth and the camera's reprojection:
// the clip space of the frame before from this one's, which the upscaler learns from the
// static meshes' world-view-projection matrices in both frames. In render pixels, from
// this frame to the one before, as FSR 3.1 takes them (motionVectorScale 1). What moves of
// its own - characters, creatures - gets the camera's motion only.
#version 460
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D warden_motion_depth;
layout(set = 0, binding = 1, rg16f) uniform writeonly image2D warden_motion_dest;
// What five pixels got - the centre and the four quarter points: depth, motion,
// clip w before - for the upscaler's log (a host-visible buffer).
layout(set = 0, binding = 2, std430) writeonly buffer WardenMotionProbe {
  vec4 probe[5];
} warden_motion_probe;

layout(push_constant) uniform WardenMotionConstants {
  // Clip space: the frame before's from this one's.
  mat4 reprojection;
  // The guest viewport: window = ndc * scale + offset (xyz; depth in z).
  vec4 viewport_scale;
  vec4 viewport_offset;
  // This frame's jitter in render pixels, right and down - the depth was drawn with it,
  // the matrices are without it.
  vec2 jitter;
  uvec2 size;
} warden_motion;

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, warden_motion.size))) {
    return;
  }
  float depth = texelFetch(warden_motion_depth, ivec2(id), 0).r;
  vec2 window = vec2(id) + 0.5 - warden_motion.jitter;
  vec3 ndc = (vec3(window, depth) - warden_motion.viewport_offset.xyz) /
             warden_motion.viewport_scale.xyz;
  vec4 previous = warden_motion.reprojection * vec4(ndc, 1.0);
  vec2 motion = vec2(0.0);
  if (previous.w > 1.0e-6) {
    vec2 previous_window = previous.xy / previous.w * warden_motion.viewport_scale.xy +
                           warden_motion.viewport_offset.xy;
    motion = previous_window - window;
  }
  imageStore(warden_motion_dest, ivec2(id), vec4(motion, 0.0, 0.0));
  uvec2 half_size = warden_motion.size / 2u, quarter = warden_motion.size / 4u;
  uvec2 probes[5] = uvec2[5](half_size, quarter, uvec2(3u * quarter.x, quarter.y),
                             uvec2(quarter.x, 3u * quarter.y), 3u * quarter);
  for (int i = 0; i < 5; ++i) {
    if (all(equal(id, probes[i]))) {
      warden_motion_probe.probe[i] = vec4(depth, motion, previous.w);
    }
  }
}
