const NEGATIVE_ZERO: float = -0.0
const POSITIVE_ZERO: float = 0.0
const ZERO_VECTOR: Vector2 = Vector2(-0.0, 0.0)
const ZERO_ARRAY: Array = [-0.0, 0.0]

func sign_of(value: float) -> float:
	return 1.0 / value

func positive_literal_first() -> void:
	var positive: float = 0.0
	var negative: float = -0.0
	print(sign_of(positive), " ", sign_of(negative))

func negative_literal_first() -> void:
	var negative: float = -0.0
	var positive: float = 0.0
	print(sign_of(negative), " ", sign_of(positive))

func test():
	positive_literal_first()
	negative_literal_first()
	print(sign_of(NEGATIVE_ZERO), " ", sign_of(POSITIVE_ZERO))
	print(sign_of(ZERO_VECTOR.x), " ", sign_of(ZERO_VECTOR.y))
	print(sign_of(ZERO_ARRAY[0]), " ", sign_of(ZERO_ARRAY[1]))
