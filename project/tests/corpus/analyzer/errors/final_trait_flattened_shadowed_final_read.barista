# A flattened trait initializer that reads a member shadowed by the implementing
# class's blank final must check the implementer's final slot before `_init()`.
extends RefCounted
uses HasId

trait HasId:
	var id: int = 0
	var copy: int = id

final var id: int

func _init() -> void:
	id = 1
