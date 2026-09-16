# Recursion also works indirectly, through a typed collection element.
enum NumberTree:
	Leaf(value: int)
	Branch(children: Array[NumberTree])

func depth(node: NumberTree) -> int:
	match node:
		NumberTree.Leaf(_):
			return 1
		NumberTree.Branch(var children):
			var best := 0
			for child: NumberTree in children:
				best = maxi(best, depth(child))
			return best + 1
	return 0

func test():
	print(depth(NumberTree.Branch([NumberTree.Leaf(1), NumberTree.Branch([NumberTree.Leaf(2)])])))
