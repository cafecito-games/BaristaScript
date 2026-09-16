# Aliases are transparent: a name for a type, expanded wherever the name is written.
type Meters = float
type Unsigned = uint | ulong
type Scalar = int | Unsigned | int
type MaybeCount = int? | uint
type MaybeCountFlipped = int | uint?


class Payload:
	var label: String = "payload"


class Shape:
	func area() -> float:
		return 0.0


class Square extends Shape:
	func area() -> float:
		return 4.0


class Circle extends Shape:
	func area() -> float:
		return 3.0


type Value = int | String | Payload
type EitherShape = Square | Circle
type NarrowInteger = int | long
type WideInteger = long | float


func describe(value: Value) -> String:
	if value is int:
		return "int"
	if value is String:
		return "String"
	return "Payload"


func classify(value: Scalar) -> String:
	if value is int:
		return "int"
	return "wide"


func measure(distance: Meters) -> Meters:
	return distance * 2.0


func total(values: Array[Meters]) -> float:
	var sum := 0.0
	for value in values:
		sum += value
	return sum


func test():
	# A single-alternative alias is indistinguishable from what it aliases, runtime typing included.
	var distance: Meters = 1.5
	prints(measure(distance), typeof(distance) == TYPE_FLOAT)

	# Nested aliases flatten into one canonical set, and a repeated alternative changes nothing.
	var scalar: Scalar = 7
	print(classify(scalar))

	# Nullability is hoisted onto the union, so `int? | uint` and `int | uint?` are the same type.
	var maybe: MaybeCount = null
	var maybe_flipped: MaybeCountFlipped = maybe
	prints(maybe == null, maybe_flipped == null)

	# A multi-alternative union erases: the value crosses the boundary with no wrapper and no tag.
	prints(describe(1), describe("text"), describe(Payload.new()))

	# The alias collapsed to `float`, so it is a valid typed-container element type.
	print(total([1.0, 2.5]))

	# A callable signature spelled with an alias behaves like the type it names.
	var doubler: Callable[[Meters], Meters] = measure
	print(doubler.call(3.0))

	# Every alternative satisfies `Shape`, so the union as a whole does.
	var either: EitherShape = Square.new()
	var shape: Shape = either
	print(shape.area())
	either = Circle.new()
	print(area_of(either))

	# A union crosses into an untyped slot unconditionally.
	var anything: Variant = either
	print(anything != null)

	# Every alternative shares the target's carrier, so widening to it converts nothing at runtime.
	var narrow: NarrowInteger = 5
	var wide: long = narrow
	prints(wide, typeof(wide) == TYPE_INT)

	# The same width-only widening holds when the target is itself a set.
	var widened: WideInteger = narrow
	prints(widened, typeof(widened) == TYPE_INT)


func area_of(shape: Shape) -> float:
	return shape.area()
