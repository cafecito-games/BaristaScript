enum Level:
	LOW = 1
	HIGH = 2

enum Shape:
	Circle(radius: int)
	Square(side: int)

func classify_bool(value: bool) -> String:
	match value:
		value is bool:
			return "bool:" + str(value)

# A plain enum is an open, integer-backed domain, so the type test covers the declared values only
# and the catch-all is what closes the match.
func classify_enum(value: Level) -> String:
	match value:
		value is Level:
			return "level:" + str(value)
		_:
			return "level:undeclared"

func classify_tagged_union(value: Shape) -> String:
	match value:
		value is Shape:
			return "shape"

func classify_variant(value: int) -> String:
	match value:
		value is Variant:
			return "variant:" + str(value)

func classify_bool_with_default(value: bool) -> String:
	match value:
		value is bool:
			return "covered:" + str(value)
		_:
			return "unreached"

func classify_bool_after_literal(value: bool) -> String:
	match value:
		false:
			return "false"
		value is bool:
			return "rest:" + str(value)

func test() -> void:
	print(classify_bool(true))
	print(classify_bool(false))
	print(classify_enum(Level.HIGH))
	print(classify_tagged_union(Shape.Circle(3)))
	print(classify_variant(7))
	print(classify_bool_with_default(true))
	print(classify_bool_after_literal(false))
	print(classify_bool_after_literal(true))
