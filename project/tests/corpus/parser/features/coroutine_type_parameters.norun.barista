# The parser accepts the `Coroutine[T]` generic type wherever a type annotation is allowed:
# local variables, parameters, return types, and nested as a container element. This pins the
# grammar surface so a regression in type-parameter parsing is caught independently of the
# analyzer or runtime.
async func _produce() -> String:
	return "value"


@warning_ignore("unused_parameter")
func _consume(p_job: Coroutine[String]) -> void:
	pass


func _start() -> Coroutine[String]:
	return _produce()


func test() -> void:
	@warning_ignore_start("unused_variable")
	var scalar: Coroutine[int]
	var void_result: Coroutine[void]
	var nullable: Coroutine[String]?
	var nested_result: Coroutine[Array[int]]
	var fan_out: Array[Coroutine[String]]
	var keyed: Dictionary[String, Coroutine[int]]
	var coroutine_keyed: Dictionary[Coroutine[int], String]
	@warning_ignore_restore("unused_variable")
