# create_proxy[T] takes exactly one type parameter, so a use-site type-argument
# list with more than one entry (`create_proxy[A, B]`) is rejected with an arity
# diagnostic rather than silently consuming only the first argument.
trait Greeter:
	abstract func greet(name: String) -> String

trait Fareweller:
	abstract func bye(name: String) -> String

func handle(_method_name: StringName, _args: Array) -> Variant:
	return null

func test() -> void:
	var bad := create_proxy[Greeter, Fareweller](handle)
	print(bad)
