extends RefCounted

# An if/else inside an existing nullable narrowing must restore the incoming
# narrowed type at the join.
func test(value: RefCounted?, condition: bool) -> void:
	if value != null:
		if condition:
			print(value.get_instance_id())
		else:
			print(value.get_instance_id())
		print(value.get_instance_id())
