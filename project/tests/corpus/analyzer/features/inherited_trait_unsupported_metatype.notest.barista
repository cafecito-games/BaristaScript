trait HiddenTypes:
	class Nested:
		pass

	tuple Pair(int, int)


class Base uses HiddenTypes:
	pass


class Child extends Base:
	pass


func class_access() -> Variant:
	return Child.Nested


func tuple_access() -> Variant:
	return Child.Pair
