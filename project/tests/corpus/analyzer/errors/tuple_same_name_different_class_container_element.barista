class Left:
	tuple Pair(a: int, b: int)


class Right:
	tuple Pair(a: int, b: int)


func test() -> void:
	var left := Left.new()
	var right := Right.new()
	var wrong: Array[Right.Pair] = [left.Pair(1, 2)]
	var wrong_value: Dictionary[String, Right.Pair] = {"k": left.Pair(3, 4)}
	print(wrong, wrong_value, right)
