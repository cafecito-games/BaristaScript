extends RefCounted

func test(value: RefCounted?) -> void:
	match value:
		null:
			pass
		RefCounted:
			print(value.get_instance_id())
		_:
			pass
