# `Array[int]?` means a typed `Array[int]` or nothing, and a literal written there is the first
# alternative, so it is typed. `null` written there still stores.
func test():
	var present: (int, Array[int]?) = (1, [])
	print(present)
	print(present[1].get_typed_builtin() == TYPE_INT)

	var absent: (int, Array[int]?) = (1, null)
	print(absent)
