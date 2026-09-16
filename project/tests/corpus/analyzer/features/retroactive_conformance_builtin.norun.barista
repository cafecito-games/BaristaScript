# Builtin value types (`int`, `Array`) are retroactively conformed to traits. Witness bodies resolve
# against the Variant builtin surface (`self.size()` on Array), and existing builtin methods auto-
# satisfy matching trait requirements (`size()` on Array needs no witness). Compiled but not run.
extend int uses RtcBuiltinCounter:
	func doubled() -> int:
		return self * 2


extend Array uses RtcBuiltinCounter:
	func doubled() -> int:
		return self.size() * 2


func take_counter(c: RtcBuiltinCounter) -> bool:
	return c != null


func use_bound[T: RtcBuiltinCounter](value: T) -> bool:
	return value != null


func test() -> void:
	var n := 3
	var c: RtcBuiltinCounter = n
	print(c != null)
	print(n is RtcBuiltinCounter)
	var as_c := n as RtcBuiltinCounter
	print(as_c != null)
	print(take_counter(n))
	print(use_bound(n))
	var a: Array[int] = [1, 2]
	print(a is RtcBuiltinCounter)
