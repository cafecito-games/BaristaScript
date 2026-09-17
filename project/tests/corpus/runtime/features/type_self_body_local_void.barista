class Base:
	func stash_self() -> void:
		var tmp: Self = self
		print(tmp == self)


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	child.stash_self()
