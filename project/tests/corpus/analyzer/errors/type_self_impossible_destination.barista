# A type parameter as the source of an assignment is decided by a runtime type test rather than
# statically, but only as far as its bound reaches: no value of `Self` bound to a class is ever an
# `int`. `self` is typed `Self` in every body of a class, so the destination is refused where it is
# written instead of compiling and failing the runtime check, in every position a value travels to. An
# ordinary bounded type parameter answers the same way, and a cast to a class the bound cannot reach
# is refused for the same reason.
class Unrelated:
	pass


class Receiver:
	var slot: int = 0

	func take(_value: int) -> void:
		pass

	func pass_argument() -> void:
		take(self)

	func initialize_local() -> void:
		var value: int = self
		print(value)

	func assign_member() -> void:
		slot = self

	func return_value() -> int:
		return self

	func from_type_parameter[T: Receiver](value: T) -> void:
		var number: int = value
		print(number)

	func cast_to_unrelated() -> void:
		var value := self as Unrelated
		print(value == null)


func test() -> void:
	Receiver.new().pass_argument()
