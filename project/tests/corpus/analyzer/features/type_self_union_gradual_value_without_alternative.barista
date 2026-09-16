# A value with no static type proves nothing, so the crossing is booked rather than proved: the union
# slot verifies membership when the store runs, and that check resolves a `Self` alternative against
# the running receiver. A union whose every alternative names `Self` therefore admits such a value on
# exactly the terms `type_self_union_gradual_value_alternative.fs` records for a union that has a
# `Self`-free alternative. The store is never reached here, so only the admission is observed.
class Receiver:
	func drive(source) -> void:
		var soft = source if false else 5
		var every_alternative: (int, Self) | (String, Self) = soft
		print(every_alternative)


func test() -> void:
	pass
