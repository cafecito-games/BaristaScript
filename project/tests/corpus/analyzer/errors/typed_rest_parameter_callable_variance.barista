# A signal is never variadic, but a variadic handler receives every signal argument past its fixed
# prefix through its rest tail, so a handler whose rest element rejects those arguments cannot be
# connected.
class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


signal animals_seen(first: Animal, second: Animal)


func on_animals_seen(...pets: Array[Dog]) -> void:
	print(pets.size())


func test() -> void:
	animals_seen.connect(on_animals_seen)
