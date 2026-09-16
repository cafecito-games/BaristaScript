# A trait requirement of `...pets: Array[Animal]` is not witnessed by an implementation that only
# accepts `Array[Dog]`, and a concrete tail never satisfies an open generic requirement.
class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


trait AcceptsAnimals:
	abstract func accept(...pets: Array[Animal]) -> long


trait AcceptsBounded:
	abstract func accept_bounded[T: Animal](...pets: Array[T]) -> long


class Narrower:
	uses AcceptsAnimals

	func accept(...pets: Array[Dog]) -> long:
		return pets.size()


class Concrete:
	uses AcceptsBounded

	func accept_bounded[U: Animal](...pets: Array[Dog]) -> long:
		return pets.size()


func test() -> void:
	print(Narrower.new().accept())
	print(Concrete.new() != null)
