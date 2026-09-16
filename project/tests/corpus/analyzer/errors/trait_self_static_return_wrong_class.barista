trait Creatable:
	abstract static func create() -> Self


class Other:
	uses Creatable

	static func create() -> Other:
		return Other.new()


class Bad:
	uses Creatable

	static func create() -> Other:
		return Other.new()


func test() -> void:
	pass
