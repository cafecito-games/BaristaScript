# A signal declared on a base class rejects a mistyped `emit()` argument when emitted from a
# subclass through the bare (implicit `self.`) identifier form.
class Base:
	signal reported(total: int)


class Child extends Base:
	func go() -> void:
		reported.emit("nope")
