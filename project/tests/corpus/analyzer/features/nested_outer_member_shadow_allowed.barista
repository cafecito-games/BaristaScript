class Outer:
	var instance_value := 1
	func compute() -> int:
		return 1
	signal changed()
	func emit_changed() -> void:
		changed.emit()
	static var shared := 2
	static func make() -> int:
		return 3

	class Inner:
		var instance_value := "instance variable allowed"
		var compute := "instance function allowed"
		var changed := "signal allowed"
		var shared := "static variable allowed"
		static func make() -> String:
			return "static function allowed"

func test() -> void:
	var inner := Outer.Inner.new()
	print(inner.instance_value)
	print(inner.compute)
	print(inner.changed)
	print(inner.shared)
	print(Outer.Inner.make())
