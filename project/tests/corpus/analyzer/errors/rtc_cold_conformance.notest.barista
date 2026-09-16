# Conforms RtcColdWidget to RtcColdTrait in the global namespace. Nothing in the fixture corpus loads
# this file, and the global namespace is never implicitly imported, so the only reason the analyzer
# knows this conformance exists is the project-wide declaration index the fixture runner fills.
extend RtcColdWidget uses RtcColdTrait:
	func cold_gadget() -> String:
		return "gadget:" + label
