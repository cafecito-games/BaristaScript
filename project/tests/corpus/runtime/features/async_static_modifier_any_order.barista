# Declaration modifiers accept any order: `async static` and `static async`
# both produce a static coroutine.
async static func async_first() -> String:
	return "async first"

static async func static_first() -> String:
	return "static first"

func test() -> void:
	print(await async_first())
	print(await static_first())
