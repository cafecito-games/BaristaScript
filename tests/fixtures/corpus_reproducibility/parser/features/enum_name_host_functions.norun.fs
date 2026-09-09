enum_name GlobalHostFunctionLevel:
	INFO = 1
	WARNING = 2

	func name() -> String:
		return "name"

	static func parse(p_name: String) -> long:
		return p_name.length()
