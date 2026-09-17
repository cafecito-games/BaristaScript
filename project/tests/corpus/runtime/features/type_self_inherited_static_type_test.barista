class Base:
	static func matches(value: Object) -> bool:
		return value is Self

	static func narrows(value: Object) -> bool:
		return (value as Self) != null


class Derived:
	extends Base


func test() -> void:
	var base := Base.new()
	var derived := Derived.new()
	print(Derived.matches(derived), " ", Derived.matches(base))
	print(Base.matches(derived), " ", Base.matches(base))
	print(Derived.narrows(derived), " ", Derived.narrows(base))
	print(Base.narrows(derived), " ", Base.narrows(base))
