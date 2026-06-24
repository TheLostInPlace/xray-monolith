-- external_emissive.s : forward additive emissive OVERLAY for external (GLTF/GLB) models.
--
-- Rendered as a second batch over a material with a glTF emissive map, IN ADDITION to its lit batch
-- (external_static/bump/mr). A forward, additive, strict-sorted pass (:sorting(2,true) routes it to
-- the 'Sorted' list, drawn by render_forward() AFTER the deferred combine), so it ADDS the emissive
-- map on top of the already-lit pixels and never touches the G-buffer. The emissive map is scaled by
-- ext_emissive_scale (= glTF emissiveFactor * emissive_strength, set per-object in the PS).
--
-- NOTE: this renders onto the tone-mapped (LDR) scene, so it does NOT bloom and values clamp at 1.
-- The engine's pre-combine emissive phase (bEmissive) WOULD give HDR+bloom, but it stencil-marks the
-- whole covered surface as self-illuminated -> the entire model renders fullbright, which is wrong
-- for a partial emissive map. True HDR bloom here would need hooking the combine itself.

function normal		(shader, t_base, t_second, t_detail)
	-- deffer_model_flat_vc carries the 2nd UV set (tc1) so the emissive PS can route a UV1 emissive map
	-- (3.5 / MultiUVTest); the plain deffer_model_flat VS does not pass tc1 through.
	shader:begin	("deffer_model_flat_vc","deffer_base_ext_emissive")
			: sorting	(2, true)							-- forward 'Sorted' list, post-combine
			: blend		(true, blend.one, blend.one)		-- additive: scene += emissive
			: zb		(true, false)						-- test against scene depth, don't write
			: dx10zfunc	(cmp_func.lessequal)				-- overlay shares the lit mesh's exact depth
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)					-- glTF emissive map
	shader:dx10sampler	("smp_base")
end
