# A value bound from a self-referential payload field carries the union's complete case set, so a
# non-exhaustive nested "match" over it is reported exactly like one over a declared parameter.
enum Chain:
	End
	Link(next: Chain)
	Branch(children: Array[Chain])
	Maker(build: Callable[[], Chain])
	Boxed(box: Box[Chain])
	Pair(both: (Chain, int))

class Box[T]:
	var value: T

	func _init(initial: T):
		value = initial

func from_parameter(node: Chain) -> int:
	match node:
		Chain.End:
			return 1
	return 0

func from_direct_bind(node: Chain) -> int:
	match node:
		Chain.Link(var next):
			match next:
				Chain.End:
					return 1
	return 0

func from_collection_bind(node: Chain) -> int:
	match node:
		Chain.Branch(var children):
			match children[0]:
				Chain.End:
					return 1
	return 0

func from_nested_bind(node: Chain) -> int:
	match node:
		Chain.Link(var next):
			match next:
				Chain.Link(var deeper):
					match deeper:
						Chain.End:
							return 1
	return 0

func from_callable_bind(node: Chain) -> int:
	match node:
		Chain.Maker(var build):
			match build.call():
				Chain.End:
					return 1
	return 0

func from_generic_bind(node: Chain) -> int:
	match node:
		Chain.Boxed(var box):
			match box.value:
				Chain.End:
					return 1
	return 0

func from_tuple_bind(node: Chain) -> int:
	match node:
		Chain.Pair(var both):
			match both[0]:
				Chain.End:
					return 1
	return 0

func make_end() -> Chain:
	return Chain.End

func test():
	print(from_parameter(Chain.End))
	print(from_direct_bind(Chain.Link(Chain.End)))
	print(from_collection_bind(Chain.Branch([Chain.End])))
	print(from_nested_bind(Chain.Link(Chain.Link(Chain.End))))
	print(from_callable_bind(Chain.Maker(make_end)))
	print(from_generic_bind(Chain.Boxed(Box[Chain].new(Chain.End))))
	print(from_tuple_bind(Chain.Pair((Chain.End, 1))))
