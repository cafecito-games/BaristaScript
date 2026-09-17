class Base:
	func clone_via_local() -> Self:
		var tmp: Self = self
		return tmp


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	print(child.clone_via_local() is Child)
