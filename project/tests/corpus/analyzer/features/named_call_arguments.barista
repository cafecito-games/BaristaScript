func greet(name: String, greeting: String) -> void:
	print(greeting + ", " + name + "!")

func combine(a: int, b: int = 10, c: int = 20) -> int:
	return a + b + c

class Calculator:
	var base_value: int

	func _init(value: int = 0) -> void:
		base_value = value

	func scaled(factor: int, offset: int = 0) -> int:
		return base_value * factor + offset

	static func describe(label: String, count: int) -> String:
		return label + ":" + str(count)

func test():
	# All arguments named and reordered.
	greet(greeting = "Hello", name = "World")
	# Positional argument followed by a named one.
	greet("Bob", greeting = "Hi")
	# Named argument fills a defaulted parameter; trailing parameter omitted.
	print(combine(1, b = 2))
	# Single named argument, the rest fall back to defaults.
	print(combine(a = 5))
	# Reorder where the trailing defaulted parameter is omitted.
	print(combine(b = 2, a = 100))
	# Named argument on a constructor call.
	var calc := Calculator.new(value = 10)
	# Named arguments on an instance method, reordered.
	print(calc.scaled(factor = 3, offset = 1))
	print(calc.scaled(offset = 5, factor = 2))
	# Named arguments on a static method, reordered.
	print(Calculator.describe(count = 7, label = "items"))
