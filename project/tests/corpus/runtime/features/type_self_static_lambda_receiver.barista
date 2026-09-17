# A lambda created in a static receiver context lexically captures that receiver, exactly like a
# closure over a local, and keeps it after the enclosing static function returns. The same compiled
# `make_factory` runs for `Base` and `Child`, but each escaped callable constructs the receiver it was
# created through -- never the declaring class, `Variant`, or whichever static receiver happens to be
# active at the call site.
class Base:
	static func make_factory() -> Callable:
		return func() -> Self:
			return Self.new()

	# A lambda that also captures an ordinary local keeps both: the lexical value and the receiver.
	static func make_greeter(prefix: String) -> Callable:
		return func() -> String:
			return "%s from %s" % [prefix, Self.new().get_class()]

	# The receiver is captured even when the only spelling of `Self` is inside the lambda signature,
	# not its body: this callable accepts and returns the exact receiver.
	static func make_echo() -> Callable:
		return func(value: Self) -> Self:
			return value

	# `Self` as a value (the class handle) travels with the callable too.
	static func make_klass() -> Callable:
		return func() -> Type[Self]:
			return Self

	# `is Self` inside the lambda resolves against the captured receiver.
	static func make_typer() -> Callable:
		return func(value: Object) -> bool:
			return value is Self

	# A nested lambda created while the outer lambda runs inherits the outer lambda's receiver, and it
	# may escape both the outer lambda and the static function before it is invoked.
	static func make_spawner() -> Callable:
		return func() -> Callable:
			return func() -> Self:
				return Self.new()


class Child:
	extends Base


class Sibling:
	extends Base

	# B. The lexical receiver beats the invocation context even when an unrelated static receiver is
	# active on the frame below: invoking a Child-captured lambda from here must still mean Child,
	# never Sibling. This guards against reading the caller's thread-local static receiver.
	static func probe_other(other_factory: Callable) -> bool:
		var made = other_factory.call()
		return made is Child


func test() -> void:
	# A. The mandatory escape path: the outer static frame is gone before the callable runs.
	var child_factory := Child.make_factory()
	var base_factory := Base.make_factory()
	print(child_factory.call() is Child)
	print(base_factory.call() is Base)

	# B. The lexical receiver beats the invocation context. These run from an ordinary (non-static)
	# function, yet still resolve to the captured receiver rather than reporting one missing.
	var made := child_factory.call()
	print(made is Child)

	# B. Invoked from inside a Sibling static frame -- a different active receiver -- the
	# Child-captured lambda still resolves to Child, never the caller's receiver.
	print(Sibling.probe_other(child_factory))

	# A. Two escaped callables created through sibling receivers keep independent receivers when
	# invoked in interleaved order; resolving one never contaminates the other.
	var sibling_factory := Sibling.make_factory()
	print(child_factory.call() is Child)
	print(sibling_factory.call() is Sibling)

	# C. Both ordinary-capture branches retain the receiver. The zero-capture branch above already
	# proved it; this one carries a captured local alongside it.
	var greeter := Child.make_greeter("hi")
	print(greeter.call())

	# D. A signature-only lambda: the body never evaluates `Self` as an expression, yet the parameter
	# and return types are validated against the captured receiver.
	var echo := Child.make_echo()
	var echoed := echo.call(Child.new())
	print(echoed is Child)

	# D. `Self` as a value (the class handle) resolves to the captured receiver.
	var klass := Child.make_klass()
	print(klass.call() == Child)

	# D. `is Self` inside the lambda uses the captured receiver.
	var typer := Child.make_typer()
	print(typer.call(Child.new()), " ", typer.call(Base.new()))

	# F. A nested lambda escapes both the outer lambda and the static function before it is invoked.
	var spawner := Child.make_spawner()
	var nested := spawner.call()
	print(nested.call() is Child)
