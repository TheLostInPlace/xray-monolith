-- external_mr.s : lit + albedo + METALLIC-ROUGHNESS (no normal map) shader for external models.
--
-- For models that carry a glTF metallic-roughness map but no normal map (e.g. the MetalRoughSpheres
-- test grid). Uses the stock flat model vertex shader (deffer_model_flat -> vertex normal, no TBN)
-- with a custom pixel shader (deffer_base_ext_mr) that feeds roughness into gloss. Only two textures
-- are needed (albedo, metal-rough), so they're passed straight through as t_base / t_second.

function normal		(shader, t_base, t_second, t_detail, t_ao)
	shader:begin	("deffer_model_flat_vc","deffer_base_ext_mr")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)		-- albedo
	shader:dx10texture	("s_bump",	t_second)	-- glTF metallic-roughness map (G=rough, B=metal)
	shader:dx10texture	("s_ao",	t_ao)		-- glTF occlusion (R); = MR tex for ORM, else separate AO
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- lit-geometry stencil mark (skybox fix)
	shader:dx10stencil_ref	(1)
end

-- Shadow-map caster for ALL light types (sun E[4] / point E[2] / spot E[3]); the engine renders every
-- shadow map from E[SE_R2_SHADOW]=E[2]=l_point, so we fill all three. Matches the stock model shadow
-- (default cull, ztest+zwrite, colour off) but uses shadow_ext_model.vs (normal-offset depth bias) so
-- neither smooth surfaces self-shadow nor thin geometry self-shadows under a close light. See
-- external_static.s for the full why.
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
