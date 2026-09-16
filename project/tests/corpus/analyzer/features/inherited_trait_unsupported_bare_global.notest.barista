trait HiddenTypes:
	class OK:
		pass

	tuple FAILED(int, int)


class Receiver uses HiddenTypes:
	func class_collision() -> Error:
		return OK

	func tuple_collision() -> Error:
		return FAILED
