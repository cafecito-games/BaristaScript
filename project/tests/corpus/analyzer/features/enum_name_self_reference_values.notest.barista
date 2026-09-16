enum_name SelfReferencingUnion:
	End
	Wrapped(value: int)

	func describe() -> String:
		match self:
			SelfReferencingUnion.Wrapped(var value):
				return "wrapped:" + str(value)
			SelfReferencingUnion.End:
				return "end"
		return "unreachable"

	static func wrap(value: int) -> SelfReferencingUnion:
		return SelfReferencingUnion.Wrapped(value)

	static func wrap_twice(value: int) -> SelfReferencingUnion:
		return SelfReferencingUnion.wrap(value + value)
