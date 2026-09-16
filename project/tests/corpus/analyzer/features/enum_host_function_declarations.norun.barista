enum Status:
	READY = 1
	DONE = 2

	func identity() -> Self:
		match self:
			READY:
				return self
			_:
				return Status.DONE

	static func normalize(value: Status) -> Status:
		return value


func test() -> void:
	var keys: Array = Status.keys()
	print(keys)
