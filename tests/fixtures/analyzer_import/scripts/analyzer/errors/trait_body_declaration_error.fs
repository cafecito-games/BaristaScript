# A trait whose own file has declaration errors cannot be flattened here. The cascading
# diagnostic must name the defining file and the underlying error so the reader is not left
# thinking trait resolution itself is unsupported.
extends RefCounted
uses CafecitoBadRequirement

func ping() -> int:
	return 1
