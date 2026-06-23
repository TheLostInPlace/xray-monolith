-- external_emissive.s : forward additive emissive OVERLAY for external (GLTF/GLB) models.
--
-- Rendered as a second batch over a material that has a glTF emissive map, IN ADDITION to its
-- normal lit batch (external_static/bump/mr). This pass is a forward, additive, sorted pass
-- (:sorting routes it to the post-deferred translucent phase, like glass/particles), so it adds the
-- emissive map on top of the already-lit pixels and never touches the G-buffer. z-test on (so it's
-- occluded by closer geometry), z-write off, no shadow element (emissive does not cast shadows).

function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("model_distort","deffer_base_ext_emissive")
			: sorting	(2, true)							-- strict-B2F -> the forward 'Sorted' list,
														--   rendered in render_forward() AFTER the
														--   deferred combine (priority 2 required for
														--   strict). :sorting(N,false) goes to a
														--   priority bucket the main render never flushes.
			: blend		(true, blend.one, blend.one)		-- additive: scene += emissive
			: zb		(true, false)						-- test against scene depth, don't write
			: fog		(false)
	shader:dx10texture	("s_base",	t_base)					-- glTF emissive map
	shader:dx10sampler	("smp_base")
end
