enum_name Issue1119RemoteStatus:
	INFO = 31
	WARN = 32
	ERROR = 33

	func name() -> String:
		match self:
			INFO:
				return "info"
			WARN:
				return "warn"
			_:
				return "error"

	static func parse(text: String) -> Self:
		if text == "warn":
			return WARN
		if text == "error":
			return ERROR
		return INFO
