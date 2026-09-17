# Builtin value types are retroactively conformed to a trait whose only requirement is *static*. Such a
# witness is not part of the builtin's own static surface, so the call misses `Variant::call_static`
# and resolves through the conformance registry instead. Two builtins are conformed separately, each
# reaching its own witness.
extend int uses RtcBuiltinBuildable:
	static func builtin_tag() -> String:
		return "int-tag"


extend String uses RtcBuiltinBuildable:
	static func builtin_tag() -> String:
		return "string-tag"


func test() -> void:
	print(int.builtin_tag())
	print(String.builtin_tag())
