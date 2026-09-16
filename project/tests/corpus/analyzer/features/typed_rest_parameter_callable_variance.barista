# A callable whose rest tail accepts a broader element type can stand in for one that accepts a
# narrower element type, because every call the target type allows is still valid. A signal is never
# variadic, but a variadic handler receives the arguments past its fixed prefix through its rest tail.
class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


signal pets_seen(first: Dog, second: Dog)

var seen := 0


func take_ints(...values: Array[int]) -> void:
	print(values.size())


func take_any(...values: Array) -> void:
	print(values.size())


func on_pets_seen(...pets: Array[Animal]) -> void:
	seen += pets.size()


func test() -> void:
	var handler := take_ints
	handler.call(1, 2)

	handler = take_any
	handler.call(1, 2, 3)

	pets_seen.connect(on_pets_seen)
	pets_seen.emit(Dog.new(), Dog.new())
	print(seen)
