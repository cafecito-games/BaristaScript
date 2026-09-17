# A destructuring declaration reads each element of the evaluated tuple into its own local: unnamed
# and named tuples behave identically, `_` slots are skipped, and `const` bindings are ordinary
# readable values.
tuple Player(name: String, hp: int)

func make_pair() -> (int, int):
	return (10, 20)

func test():
	var (x, y) = make_pair()
	print(x)
	print(y)

	var (name, hp) = Player("Ana", 7)
	print(name)
	print(hp)

	var (_, middle, _) = (1, "middle", true)
	print(middle)

	const (first, second) = (2, 3)
	print(first + second)

	x = x + y
	print(x)

	for index in range(2):
		var (loop_first, loop_second) = (index, index * 2)
		print(loop_first + loop_second)
