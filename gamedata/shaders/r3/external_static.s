-- external_static.s : Phase 1 shader for FExternalVisual (GLTF/GLB static meshes).
--
-- Consumes the engine's standard model vertex layout (position FLOAT4, normal/tangent/
-- binormal D3DCOLOR, texcoord FLOAT2 -- see FExternalVisual.cpp dwDecl_External), so it
-- reuses the stock deferred MODEL vertex/pixel shaders (deffer_model_flat / deffer_base_flat).
-- emissive(true) writes the mesh to the emissive buffer => it renders fullbright and is
-- visible regardless of scene lighting, which is exactly what we want to confirm geometry,
-- UVs and the base texture in Phase 1. Phase 2 (pbr_external) replaces this with a lit,
-- normal/roughness/metallic shader.
--
-- All four pass names below are confirmed to exist in stock gamedata (they are referenced
-- by models_selflight_det.s).

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_flat")
			: fog		(false)
			: emissive 	(true)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	( 	true, cmp_func.always,
							255 , 127,
							stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref	(1)

end

function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_direct_model",	"accum_emissive_det")
			: zb 		(true,false)
			: fog		(false)
			: emissive 	(true)

end
