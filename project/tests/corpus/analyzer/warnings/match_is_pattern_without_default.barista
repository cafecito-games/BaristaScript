func partial(value: Variant) -> void:
	match value:
		value is int:
			print("int")

func covering(value: Variant) -> void:
	match value:
		value is Variant:
			print("anything")

func test() -> void:
	partial(1)
	covering(1)
