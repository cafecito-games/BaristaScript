@warning_ignore("mixed_namespace_directory")
class_name TestExportTaggedUnionMetatypeAsDictionary

enum Message:
	Quit
	Move(x: int, y: int)

@export var message_type = Message

func test():
	for property in get_property_list():
		if str(property.name).begins_with("message_type"):
			Utils.print_property_extended_info(property)
