-- external_static_clamp.s : CLAMP-sampler variant of external_static.s. Selected when the glTF material's
-- base-color texture uses a sampler with wrap_s = wrap_t = CLAMP_TO_EDGE (4.2). Identical to
-- external_static.s except smp_base is clamped (no edge wrap). The .s script API only exposes :clamp()
-- (wrap/mirror are not bound), so MIRRORED_REPEAT falls back to the default (repeat) variant.

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat_vc","deffer_base_ext_static")
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)
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
