enum_name BarePayloadCaseUnion:
	End
	Wrapped(value: int)

	static func wrap(value: int) -> BarePayloadCaseUnion:
		return Wrapped(value)
