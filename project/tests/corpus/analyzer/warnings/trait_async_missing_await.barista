# Calling a flattened concrete async trait method without "await" raises the
# MISSING_AWAIT warning, just like a regular coroutine call.
extends RefCounted
uses Fetcher

trait Fetcher:
	async func fetch() -> String:
		return "data"

func test():
	fetch()
