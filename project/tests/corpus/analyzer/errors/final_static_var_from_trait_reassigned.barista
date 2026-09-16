# A `final static var` declared in a trait flattens into the implementing class's static storage and
# is write-once there; reassigning it from a method is a violation.
extends RefCounted
uses HasConfig

trait HasConfig:
	final static var VERSION := 1

func bump() -> void:
	VERSION = 2

func test() -> void:
	pass
