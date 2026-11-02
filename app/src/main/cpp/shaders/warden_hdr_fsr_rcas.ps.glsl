// PLAN 9.2: Xenia's FSR 1 sharpening pass, RCAS (guest_output_ffx_fsr_rcas.xesli,
// untouched), its result encoded for HDR10 - see warden_hdr_bilinear.ps.glsl for how.
#version 460
#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

#define main warden_sdr_main
#include "guest_output_ffx_fsr_rcas.xesli"
#undef main

#include "warden_hdr_output.glsli"

void main() {
  warden_sdr_main();
  xe_fsr_rcas_color.rgb = WardenHdrEncode(xe_fsr_rcas_color.rgb);
}
