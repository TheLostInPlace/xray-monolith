-- external_skinned.s : lit + textured shader for SKINNED external (GLTF/GLB) mesh geometry.
--
-- IDENTICAL bindings to external_static.s (same deffer_model_flat VS + deffer_base_ext_flat PS + the
-- shadow caster). The ONLY reason this is a separate .s is the shader cache: the compiled .s (and the
-- model VS it binds) is cached by NAME, and the global SkinningMode is read at VS-compile time. Static
-- external models compile external_static as SKIN_NONE; if a skinned model reused external_static it would
-- get that cached SKIN_NONE VS (no skinning -> mesh renders in raw/un-posed vertex space). By using a
-- DEDICATED name that ONLY skinned models load (always with SetSkinningMode(4)), this compiles as the
-- SKIN_4 variant -- so deffer_model_flat reads sbones_array and deforms the mesh. The PS is skin-agnostic.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_ext_flat")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	-- lit-geometry stencil mark (skybox fix), EXACTLY as the stock deferred model blender does
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)
	shader:dx10stencil_ref	(1)
end

-- Shadow-map caster for ALL light types (sun cascade E[4] / point E[2] / spot E[3]); engine renders every
-- shadow map from E[SE_R2_SHADOW]=E[2]=l_point. Same stock caster setup but our shadow_ext_model.vs (now
-- with SKIN_0..4 mains, so the shadow also skins under SetSkinningMode(4)) + normal-offset bias.
function l_point	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_ext_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
l_spot    = l_point
l_special = l_point
