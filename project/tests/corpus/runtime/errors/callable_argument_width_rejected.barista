# The call boundary converts each argument through the same structural test, so a parameter that
# declares a width rejects an out-of-range integer instead of storing a value it cannot hold. The
# in-range call goes through first, proving the parameter is otherwise callable through a Callable.
func take(value: int) -> void:
	print(value)


func test() -> void:
	var callback: Callable = take
	callback.call(2147483647)
	callback.call(9223372036854775807)
	print("unreachable")
