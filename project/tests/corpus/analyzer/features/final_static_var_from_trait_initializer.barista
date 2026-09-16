# A `final static var` with an initializer supplied by a trait flattens into the implementer's static
# storage with its slot already filled; reading it from the implementing class is valid.
extends RefCounted
uses HasConfig

trait HasConfig:
	final static var VERSION := 3

func test() -> void:
	print(VERSION)
