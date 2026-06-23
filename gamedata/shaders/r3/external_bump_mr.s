-- external_bump_mr.s : lit + albedo + NORMAL MAP + METALLIC-ROUGHNESS shader for external models.
--
-- Like external_bump.s but the G-buffer pass also feeds the glTF metallic-roughness map into gloss
-- (deffer_base_ext_bump_mr). Selected by the C++ loader when the material has BOTH a normal map and
-- a metallic-roughness map. All three maps are passed explicitly in the texture list
-- "albedo,normal,metalrough"; the engine forwards the 3rd list texture as a 5th arg (t_metalrough).

function normal		(shader, t_base, t_second, t_detail, t_metalrough)
	shader:begin	("deffer_model_bump","deffer_base_ext_bump_mr")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)			-- albedo
	shader:dx10texture	("s_bump",	t_second)		-- glTF tangent-space normal map
	shader:dx10texture	("s_bumpX",	t_metalrough)	-- glTF metallic-roughness map (G=rough, B=metal)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- lit-geometry stencil mark (skybox fix)
	shader:dx10stencil_ref	(1)
end

function l_special	(shader, t_base, t_second, t_detail, t_metalrough)
	-- Sun shadow caster: depth only, back-face cast (no self-shadow acne). Identical to external_bump.
	shader:begin	("shadow_direct_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
			: dx10cullmode	(2)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
