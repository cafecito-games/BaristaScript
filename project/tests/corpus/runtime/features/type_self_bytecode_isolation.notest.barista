class Crate[T]:
	var value: T


class Base:
	static func ordinary_label() -> String:
		return "base"

	static func spawn() -> Self:
		return Self.new()

	static func accept(value: Self) -> Self:
		return value

	static func pack(value: Self) -> Crate[Self]:
		var crate := Crate[Self].new()
		crate.value = value
		return crate

	static func nested_types(
		values: Array[Self],
		by_name: Dictionary[String, Self],
		handle: Type[Self],
		factory: Callable[[Self], Self],
	) -> Crate[Array[Type[Self]]]:
		return Crate[Array[Type[Self]]].new()

	static func invoke(factory: Callable[[], Self]) -> Self:
		return factory.call()

	static async func resume_after(source: Object) -> Self:
		await source.script_changed
		return Self.new()


class Derived:
	extends Base


trait RootFactory:
	abstract static func root_make() -> Self


trait LeafFactory:
	uses RootFactory
	abstract static func leaf_make() -> Self


class WitnessBase:
	pass


class WitnessDerived:
	extends WitnessBase


extend WitnessBase uses LeafFactory:
	static func root_make() -> Self:
		return Self.new()

	static func leaf_make() -> Self:
		return Self.new()
