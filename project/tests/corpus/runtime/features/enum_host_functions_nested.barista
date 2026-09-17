class Left:
	enum LeftStatus:
		READY = 11

		func describe() -> String:
			return "left:" + str(self)

		static func initial() -> Self:
			return READY


class Right:
	enum RightStatus:
		READY = 22

		func describe() -> String:
			return "right:" + str(self)

		static func initial() -> Self:
			return READY


func test() -> void:
	var left: Left.LeftStatus = Left.LeftStatus.initial()
	var right: Right.RightStatus = Right.RightStatus.initial()
	print(left.describe())
	print(right.describe())
