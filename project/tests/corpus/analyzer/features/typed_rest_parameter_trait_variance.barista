# A trait witness is held to the same contravariant rest rule as a class override: the same element
# type, a broader one, or a gradual tail all satisfy a typed requirement. A generic requirement is
# matched up to alpha-renaming of its method type parameters.
class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


trait AcceptsDogs:
	abstract func accept(...pets: Array[Dog]) -> int


trait AcceptsBounded:
	abstract func accept_bounded[T: Animal](...pets: Array[T]) -> int


class SameElement:
	uses AcceptsDogs

	func accept(...pets: Array[Dog]) -> int:
		return 10 + pets.size()


class BroaderElement:
	uses AcceptsDogs

	func accept(...pets: Array[Animal]) -> int:
		return 20 + pets.size()


class GradualTail:
	uses AcceptsDogs

	func accept(...pets: Array) -> int:
		return 30 + pets.size()


class RenamedParameter:
	uses AcceptsBounded

	func accept_bounded[U: Animal](...pets: Array[U]) -> int:
		return 40 + pets.size()


func test() -> void:
	var same: AcceptsDogs = SameElement.new()
	print(same.accept(Dog.new(), Dog.new()))

	var broader: AcceptsDogs = BroaderElement.new()
	print(broader.accept(Dog.new()))

	var gradual: AcceptsDogs = GradualTail.new()
	print(gradual.accept(Dog.new(), Dog.new(), Dog.new()))

	print(RenamedParameter.new().accept_bounded[Dog](Dog.new()))
