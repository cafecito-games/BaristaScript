# Converting an argument produces the parameter's carrier but not necessarily a value the parameter's
# declared width can hold. The largest float that truncates into range goes through first, proving
# the conversion itself is intact, and the next magnitude up is rejected instead of being stored.
func take(value: int) -> void:
	print(value)


func test() -> void:
	var callback: Callable = take
	callback.call(2147483647.0)
	callback.call(2147483648.0)
	print("unreachable")
