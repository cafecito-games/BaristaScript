# A tagged-union payload's `Self` field is receiver-relative at construction, exactly as a `Self`
# call parameter is: the unqualified, `self.`-qualified, and contextual shorthand spellings admit
# the receiver itself in every `Self` position, `null` for a nullable `Self?` field, and carry the
# rule recursively through unnamed tuple element positions.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)
		Opt(index: int, owner: Self?)
		Deep(index: int, pair: (String, Self))

	func construct_all() -> void:
		var made := Message.Attach(1, self)
		if made is Message.Attach(index, owner):
			prints("made", index, owner == self)
		var qualified := self.Message.Attach(2, self)
		if qualified is Message.Attach(index, owner):
			prints("qualified", index, owner == self)
		var shorthand: Message = .Attach(3, self)
		if shorthand is Message.Attach(index, owner):
			prints("shorthand", index, owner == self)
		var deep := Message.Deep(4, ("a", self))
		if deep is Message.Deep(index, pair):
			prints("deep", index, pair.0)
		var optional := Message.Opt(5, null)
		if optional is Message.Opt(index, owner):
			prints("optional", index, owner == null)


func test() -> void:
	Receiver.new().construct_all()
