# A class member's declared tuple type types its initializer literal.
var slot: (int, Array[int]) = (1, [])


func test():
	print(slot)
	print(slot[1].get_typed_builtin() == TYPE_INT)
