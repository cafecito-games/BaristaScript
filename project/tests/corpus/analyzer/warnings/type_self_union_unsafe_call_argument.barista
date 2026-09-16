# An alternative of a union that names no `Self` admits whatever it would admit on its own, a value
# whose static type promises nothing included. Nothing is proved about such a value, so the crossing is
# reported exactly as it is for a union that mentions no `Self` at all.
class Receiver:
	func take(_value: int | (int, Self)) -> void:
		pass

	func take_plain(_value: int | String) -> void:
		pass

	func drive(dynamic) -> void:
		take(dynamic)
		take_plain(dynamic)


func test() -> void:
	pass
