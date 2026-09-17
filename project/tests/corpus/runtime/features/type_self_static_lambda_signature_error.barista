# Signature validation through an escaped lambda names the captured receiver. The lambda's `Self`
# parameter is resolved against the receiver the lambda was created through (`Child`), so a `Base`
# argument is rejected and the diagnostic identifies `Child` -- not `Self`, `Base`, the declaration
# target, or `Variant`.
class Base:
	static func make_echo() -> Callable:
		return func(value: Self) -> Self:
			return value


class Child:
	extends Base


func test() -> void:
	var echo := Child.make_echo()
	echo.call(Base.new())
