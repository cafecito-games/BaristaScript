extends RefCounted

class WithStatic:
	uses Registry

class WithoutStatic:
	pass

trait Registry:
	static var count: int = 0
	static func bump() -> void:
		count += 1

func test() -> void:
	WithStatic.bump()
	WithStatic.bump()
	print(WithStatic.count)
