-- external_bump_mr.s : lit + albedo + NORMAL MAP + METALLIC-ROUGHNESS shader for external models.
--
-- Like external_bump.s but the G-buffer pass also feeds the glTF metallic-roughness map into gloss
-- (deffer_base_ext_bump_mr). Selected by the C++ loader only when the material has BOTH a normal map
-- and a metallic-roughness map. The texture list passed in is still "albedo,normal" (t_base/t_second);
-- the engine's .s->Lua bridge only forwards two list textures, so the third (metal-rough) texture's
-- $user$ name is DERIVED here from the normal name -- the loader registers it under the matching
-- "$user$gltf_mr\..." name (vs the normal map's "$user$gltf_n\..."). Both always exist in this path.

function normal		(shader, t_base, t_second, t_detail)
	local t_mr = (t_second:gsub("gltf_n", "gltf_mr", 1))	-- "$user$gltf_n\X" -> "$user$gltf_mr\X"
	shader:begin	("deffer_model_bump","deffer_base_ext_bump_mr")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)		-- albedo
	shader:dx10texture	("s_bump",	t_second)	-- glTF tangent-space normal map
	shader:dx10texture	("s_bumpX",	t_mr)		-- glTF metallic-roughness map (G=rough, B=metal)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)  -- lit-geometry stencil mark (skybox fix)
	shader:dx10stencil_ref	(1)
end

function l_special	(shader, t_base, t_second, t_detail)
	-- Sun shadow caster: depth only, back-face cast (no self-shadow acne). Identical to external_bump.
	shader:begin	("shadow_direct_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
			: dx10cullmode	(2)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10color_write_enable	(false, false, false, false)
end
