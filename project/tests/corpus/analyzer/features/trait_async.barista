# async and abstract async trait methods are permitted and integrate with the
# async contract: a required async method is satisfied by an async implementation,
# a concrete async method is flattened, transitive async traits resolve, and calls
# to async trait methods are awaited.
extends RefCounted
uses Loader

trait Fetchable:
	abstract async func fetch() -> String

trait Loader:
	uses Fetchable

	async func load_data() -> String:
		return await fetch()

async func fetch() -> String:
	return "data"

func run() -> void:
	var _from_load := await load_data()
	var _direct := await fetch()

func test() -> void:
	pass
