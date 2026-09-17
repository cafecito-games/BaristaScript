# A static function reached through a callable is found the same way a direct static call finds it,
# so its declared parameter is available to the diagnostic too.
static func take(value: Dictionary[String, int]) -> void:
	print("took ", value)


func test() -> void:
	var callback: Callable = take
	callback.call({"one": 1} as Dictionary[String, int])
	callback.call({"one": 1.5} as Dictionary[String, float])
	print("unreachable")
