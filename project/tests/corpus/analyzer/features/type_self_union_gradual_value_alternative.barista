# A value with no static type proves nothing, so the crossing is booked rather than proved: the union
# slot verifies membership when the store runs. A declared variable, an assignment target, and a
# return type all agree, and so does a union whose every alternative names `Self`
# (`type_self_union_gradual_value_without_alternative.fs`).
class Receiver:
	var counter = 5

	func make() -> int | (int, Self):
		var soft = counter
		return soft

	func drive() -> void:
		var soft = counter
		var link: int | (int, Self) = soft
		print("initialized ", link)
		link = soft
		print("assigned ", link)
		print("returned ", make())


func test() -> void:
	Receiver.new().drive()
