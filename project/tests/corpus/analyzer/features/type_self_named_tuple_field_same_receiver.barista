# A named tuple's `Self` field is receiver-relative at construction, exactly as a `Self` call
# parameter is: the unqualified and `self.`-qualified spellings admit the receiver itself in every
# `Self` position, `null` for a nullable `Self?` field, and carry the rule recursively through
# unnamed tuple element positions.
class Receiver:
	tuple Pair(index: int, owner: Self)
	tuple OptPair(index: int, owner: Self?)
	tuple Deep(index: int, pair: (String, Self))

	func construct_all() -> void:
		var made := Pair(1, self)
		print("made ", made.index, " ", made.owner == self)
		var qualified := self.Pair(2, self)
		print("qualified ", qualified.index, " ", qualified.owner == self)
		var deep := Deep(3, ("a", self))
		print("deep ", deep.index, " ", deep.pair.0)
		var optional := OptPair(4, null)
		print("optional ", optional.index, " ", optional.owner == null)


func test() -> void:
	Receiver.new().construct_all()
