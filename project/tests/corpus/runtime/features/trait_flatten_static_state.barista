extends RefCounted
uses Registry

trait Registry:
	static var count: int = 0
	static func register() -> void:
		count += 1

func test() -> void:
	print(count)
	register()
	register()
	print(count)
