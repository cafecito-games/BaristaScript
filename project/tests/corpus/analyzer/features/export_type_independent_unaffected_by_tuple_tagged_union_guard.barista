@warning_ignore("mixed_namespace_directory")
class_name TestExportTypeIndependentUnaffectedByTupleTaggedUnionGuard

tuple Vec2(x: float, y: float)

enum Message:
	Quit
	Move(x: int, y: int)

@export_storage var position: Vec2 = Vec2(1.0, 2.0)
@export_storage var last_message: Message
@export_storage var history: Array[Message] = []

@export_custom(PROPERTY_HINT_NONE, "") var custom_position: Vec2 = Vec2(1.0, 2.0)
@export_custom(PROPERTY_HINT_NONE, "") var custom_history: Array[Message] = []

func test():
	for property in get_property_list():
		var name := str(property.name)
		if name.begins_with("position") or name.begins_with("last_message") or name.begins_with("history") or name.begins_with("custom_"):
			Utils.print_property_extended_info(property)
