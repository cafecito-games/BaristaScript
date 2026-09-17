# A class-handle parameter is an `Object` in carrier terms, so only the declaration can say that the
# call wanted the class itself rather than an instance of it.
func take(klass: Type[RefCounted]) -> void:
	print("took ", klass != null)


func test() -> void:
	var callback: Callable = take
	callback.call(RefCounted)
	callback.call(RefCounted.new())
	print("unreachable")
