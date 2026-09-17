# The return slot is checked at its own boundary. The caller binds the result as `Variant`, so
# nothing downstream would ever look at the shape; the failure is reported inside `corrupted()`.
func corrupted(value: Variant) -> (int, String):
	return value


func test() -> void:
	var escaped: Variant = corrupted(("wrong", "kind"))
	print(escaped)
