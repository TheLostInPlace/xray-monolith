-- external_blend.s : forward lit + alpha-BLENDED pass for external (GLTF/GLB) models whose material is
-- glTF alphaMode = BLEND.
--
-- A transparent surface can't go in the deferred G-buffer, so (unlike opaque/MASK, which render the
-- deferred batch) a BLEND material renders ONLY through this forward pass: src-alpha blended, strict
-- back-to-front sorted, depth-tested against the scene but not depth-writing (so it doesn't occlude and
-- multiple transparent layers composite). Same family as the emissive/metal overlays, but this IS the
-- surface, not an additive extra. See deffer_base_ext_blend.ps. No shadow caster (transparent).

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_ext_blend")
			: sorting	(2, true)									-- forward 'Sorted' list, back-to-front
			: blend		(true, blend.srcalpha, blend.invsrcalpha)	-- standard src-over alpha blend
			: zb		(true, false)								-- test scene depth, don't write
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)							-- albedo (rgb + opacity in .a)
	shader:dx10sampler	("smp_base")
end
