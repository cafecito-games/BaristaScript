extends RefCounted
uses TraitA, TraitB

trait TraitA:
	var health: int
	abstract func collide(value: int) -> int

trait TraitB:
	var health: int
	abstract func collide(value: int) -> int

var health: int

func collide(value: int) -> int:
	return value

func test() -> void:
	print(collide(1) + health)
