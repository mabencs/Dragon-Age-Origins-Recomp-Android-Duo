// PLAN 9.2: Xenia's bilinear pass (guest_output_bilinear.xesli, untouched), its result
// encoded for HDR10 by warden_hdr_output.glsli. Xenia's GLSL entry macro spells its
// function `main`, so renaming the token while the pass is included makes it a function
// this shader calls first. No dithering: the HDR10 swapchain is 10-bit.
#version 460
#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

#define main warden_sdr_main
#include "guest_output_bilinear.xesli"
#undef main

#include "warden_hdr_output.glsli"

void main() {
  warden_sdr_main();
  xe_bilinear_color.rgb = WardenHdrEncode(xe_bilinear_color.rgb);
}
