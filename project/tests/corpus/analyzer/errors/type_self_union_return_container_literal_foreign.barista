# The alternative a returned literal is built against keeps its own rules: an unrelated element and a
# carrier built as the bound are rejected exactly as they are for a plain `Array[Self]` return type.
class Foreign:
	pass


class Receiver:
	func make_foreign() -> Array[Self] | int:
		return [Foreign.new()]

	func make_bound() -> Array[Self] | int:
		var bound: Array[Receiver] = [Receiver.new()]
		return bound


func test() -> void:
	pass
