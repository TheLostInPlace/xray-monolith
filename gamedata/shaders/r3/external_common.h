// external_common.h : shared per-material constants + helpers for external (glTF/GLB) model shaders.
// All are set per-object in FExternalVisual::Render (RCache.set_c by name). Defaults are written every
// draw, so a shader that includes this and uses ext_uv()/the factors is always given sane values.

#ifndef EXTERNAL_COMMON_H
#define EXTERNAL_COMMON_H

float4 ext_base_color;    // rgb = glTF baseColorFactor (albedo/F0 tint), a = baseColorFactor.a (MASK alpha).
float4 ext_mr_factor;     // x = metallicFactor, y = roughnessFactor, z = normalScale. Default (1,1,1,_).
float4 ext_uv_transform;  // KHR_texture_transform: xy = scale, zw = offset. Default (1,1,0,0) = identity.
float4 ext_uv_rot;        // KHR_texture_transform rotation: x = cos, y = sin. Default (1,0,0,0) = identity.
float4 ext_uv_set;        // 3.5 per-map texCoord: x=base, y=normal, z=mr, w=ao (0 = UV0, 1 = UV1). Default 0.

// Apply KHR_texture_transform (scale, then rotate, then offset -- the glTF T*R*S order) to a UV before
// sampling. Default constants make this the identity transform, so untransformed maps are unchanged.
float2 ext_uv( float2 uv )
{
	float2 s = uv * ext_uv_transform.xy;                              // scale
	float2 r = float2( ext_uv_rot.x * s.x - ext_uv_rot.y * s.y,       // rotate (x=cos, y=sin)
	                   ext_uv_rot.y * s.x + ext_uv_rot.x * s.y );
	return r + ext_uv_transform.zw;                                  // offset
}

// 3.5 per-map texCoord: pick the KHR-transformed UV0 or the raw UV1 for a map, by its selector (0 or 1).
float2 ext_uv_pick( float2 uv0, float2 uv1, float sel )
{
	return (sel < 0.5) ? uv0 : uv1;
}

// How much of a metal's diffuse to actually suppress (1 = pure PBR, no diffuse; 0 = keep all diffuse).
// Pure-PBR metals reflect only the environment (our sky cube), so in a dark scene lit only by the
// flashlight they go black -- a dynamic light can't diffuse-light a diffuse-less surface. Keeping a
// fraction of diffuse lets the flashlight reveal metals. Trades mirror-purity for visibility, which is
// the right call for a dark, flashlight-driven game. Tune here (lower = more visible / less mirror).
#define EXT_METAL_DIFFUSE_KILL 0.6h

#endif
