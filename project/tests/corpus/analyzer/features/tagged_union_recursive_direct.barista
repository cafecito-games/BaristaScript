# A tagged union's payload field may name the union itself: the payload holds another
# `[tag, payload...]` read-only array, so a value stays finite.
enum Chain:
	End
	Link(next: Chain)

func length(node: Chain) -> int:
	match node:
		Chain.End:
			return 0
		Chain.Link(var next):
			return 1 + length(next)
	return 0

func test():
	print(length(Chain.Link(Chain.Link(Chain.End))))
