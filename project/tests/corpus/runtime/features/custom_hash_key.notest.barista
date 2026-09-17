class_name CustomHashKey
extends RefCounted

var id: int
var label: String


func _init(p_id: int, p_label: String) -> void:
	id = p_id
	label = p_label


func _equals(other: Variant) -> bool:
	return other is CustomHashKey and id == other.id and label == other.label


func _hash_code() -> long:
	return hash("%s:%s" % [id, label])
