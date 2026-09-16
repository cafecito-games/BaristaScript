# A subclass may declare and assign its own final while inheriting (and reading) a
# base final; each final fills its own slot in its own declaring class.
class Base:
	final var id := 1

class Derived extends Base:
	final var label: String

	func _init() -> void:
		label = "derived-%d" % id

func test() -> void:
	var derived := Derived.new()
	print(derived.id)
	print(derived.label)
