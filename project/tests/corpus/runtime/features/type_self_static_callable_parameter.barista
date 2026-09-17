class Base:
	static func take(value: Self) -> void:
		print(value is Child)


class Child:
	extends Base


func test() -> void:
	var callable := Child.take
	callable.call(Child.new())
