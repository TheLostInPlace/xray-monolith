-- external_bump_clamp.s : CLAMP-sampler variant of external_bump.s (4.2). Selected when the material's
-- base-color sampler is CLAMP_TO_EDGE on both axes. Same bindings; smp_base is clamped.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_bump_vc","deffer_base_ext_bump")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)		-- albedo
	shader:dx10texture	("s_bump",	t_second)	-- glTF tangent-space normal map
	shader:dx10sampler	("smp_base"):clamp()
	shader:dx10stencil	(true, 8, 255, 127, 1, 3, 1)
	shader:dx10stencil_ref	(1)
end

function l_point	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_ext_model",	"dumb")
			: zb		(true,true)
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base"):clamp()
	shader:dx10color_write_enable	(false, false, false, false)
end
l_spot    = l_point
l_special = l_point
