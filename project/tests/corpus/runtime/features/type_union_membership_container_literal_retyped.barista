# A union slot accepts exactly what its alternatives accept standing alone. A plain `Array[int]`
# parameter retypes an untyped literal at its binding, so a union holding that alternative retypes it
# too -- otherwise the union would be stricter than the alternative it is built from. Alternatives are
# tried in the analyzer's canonical order, so a literal several of them could claim lands in the first
# that admits it, and a literal none of them describes is still rejected.
func absorb(entries: Array[int] | Array[String]) -> void:
	print("absorbed ", entries)


func test() -> void:
	absorb([1, 2])
	absorb(["a", "b"])
	absorb([])
