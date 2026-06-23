-- external_mr.s : lit + albedo + METALLIC-ROUGHNESS (no normal map) shader for external models.
--
-- For models that carry a glTF metallic-roughness map but no normal map (e.g. the MetalRoughSpheres
-- test grid). Uses the stock flat model vertex shader (deffer_model_flat -> vertex normal, no TBN)
-- with a custom pixel shader (deffer_base_ext_mr) that feeds roughness into gloss. Only two textures
-- are needed (albedo, metal-rough), so they're passed straight through as t_base / t_second.

function normal		(shader, t_base, t_second, t_detail, t_ao)
	shader:begin	("deffer_model_flat","deffer_base_ext_mr")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)		-- albedo
	shader:dx10texture	("s_bump",	t_second)	-- glTF metallic-roughness map (G=rough, B=metal)
	shader:dx10texture	("s_ao",	t_ao)		-- glTF occlusion (R); = MR tex for ORM, else separate AO
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- lit-geometry stencil mark (skybox fix)
	shader:dx10stencil_ref	(1)
end

function l_special	(shader, t_base, t_second, t_detail)
	-- Sun shadow caster: depth only, back-face cast (no self-shadow acne).
	shader:begin	("shadow_direct_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
			: dx10cullmode	(2)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
