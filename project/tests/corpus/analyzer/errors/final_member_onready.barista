# `@onready` defers initialization to `_ready()`, after `_init()`, which is
# outside the slot the write-once analysis reasons about.
extends Node

@onready final var label := "ready"

func test() -> void:
	pass
