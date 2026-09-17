# A native engine class is retroactively conformed to a trait whose only requirement is *static*. Such
# a witness has no `MethodBind`, so it cannot ride the native-static call path; it resolves through the
# conformance registry and dispatches with no instance. `Resource.native_tag()` additionally proves the
# engine inheritance walk: the witness is declared on `RefCounted`, a base of `Resource`.
extend RefCounted uses RtcNativeBuildable:
	static func native_tag() -> String:
		return "refcounted-tag"


func test() -> void:
	print(RefCounted.native_tag())
	print(Resource.native_tag())
