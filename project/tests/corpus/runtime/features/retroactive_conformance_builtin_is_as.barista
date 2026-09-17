# `is`/`as` against retroactively-conformed builtin values, including through a `Variant`-typed local.
extend int uses RtcBuiltinIsAs:
	func marker() -> int:
		return 1


func test() -> void:
	var n := 3
	print(n is RtcBuiltinIsAs)
	var as_trait := n as RtcBuiltinIsAs
	print(as_trait != null)
	var v: Variant = n
	print(v is RtcBuiltinIsAs)
	print(4.0 is RtcBuiltinIsAs)
	print(n is RtcBuiltinOther)
