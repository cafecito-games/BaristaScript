# A receiver typed `Self` contributes the frame's own `Self` to the parameter contract, so an alias
# receiver matches the contract and is rejected for the one reason a rendered type cannot show:
# nothing proves the alias local holds the calling frame's receiver. Supplying the alias itself stays
# accepted, and so do the unqualified and `self.`-qualified spellings.
class Cell:
	func take(other: Self) -> void:
		prints(other == self)

	func unqualified() -> void:
		take(self)

	func qualified() -> void:
		self.take(self)

	func aliased() -> void:
		var alias := self
		alias.take(self)

	func alias_typed_base() -> void:
		var alias: Cell = self
		alias.take(self)

	func aliased_receiver_argument() -> void:
		var alias := self
		alias.take(alias)


func test() -> void:
	Cell.new().unqualified()
