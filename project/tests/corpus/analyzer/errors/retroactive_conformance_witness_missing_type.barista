# Negative control for the witness declaration-site fallback: when neither the conformance target's
# scope nor the declaring file supplies the name, the witness keeps the ordinary hard diagnostic
# instead of widening the annotation to Variant.
extend RtcGadget uses RtcNeedsPing:
	func ping() -> void:
		var helper: MissingHelper = null
		print(helper)
