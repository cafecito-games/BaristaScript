# A signal declared on a base class validates `emit()` argument types identically when emitted
# from a subclass through the bare (implicit `self.`) identifier form. Issue #1690.
func test() -> void:
	var child := Child.new()
	child.go()
	print("No failure")


class Base:
	signal reported(total: int)


class Child extends Base:
	func go() -> void:
		reported.emit(1)
