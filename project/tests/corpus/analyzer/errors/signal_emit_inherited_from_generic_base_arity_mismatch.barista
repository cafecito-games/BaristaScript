# Specializing an inherited generic signal's parameters leaves the existing `emit()` argument-count
# diagnostics unchanged.
class Base[T]:
	signal reported(value: T, label: String)


class Child extends Base[int]:
	func go() -> void:
		reported.emit(1)
		reported.emit(1, "ok", 2)
