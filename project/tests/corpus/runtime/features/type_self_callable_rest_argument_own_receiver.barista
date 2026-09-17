# A callable whose variadic tail is typed with the calling frame's `Self` reaches an own-receiver
# parameter of the same shape and dispatches, with the tail reified to the running leaf.
class Base:
	func fan(callback: Callable[[...Array[Self]], void]) -> void:
		callback.call(self, self)

	func spread(...values: Array[Self]) -> void:
		print("spread ", values.size(), " ", values.get_typed_script() == Child)

	func run() -> void:
		var callback: Callable[[...Array[Self]], void] = spread
		fan(callback)


class Child:
	extends Base


func test() -> void:
	Child.new().run()
