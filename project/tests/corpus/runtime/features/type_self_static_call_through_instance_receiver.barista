# A `static func` whose signature references `Self`, called through an instance receiver rather than
# the class handle, resolves `Self` against the receiver. The instance names a class exactly as a
# class handle does, so the call dispatches just as `Child.take(...)` would instead of failing for
# want of a receiver.
#
# `Self` binds to the receiver's runtime class, matching how an instance method reached through the
# same receiver resolves it. Analysis substitutes the receiver's *static* type, which is always a
# base of that runtime class, so the static promise stays an upper bound of what the frame sees.
@warning_ignore_start("static_called_on_instance")


class Base:
	signal done(value: Base)

	static func take(value: Self) -> void:
		print("parameter is Child: %s" % [value is Child])

	static func take_rest(...values: Array[Self]) -> void:
		print("rest tail is Child: %s" % [values.get_typed_script() == Child])

	static func make() -> Self:
		return Self.new()

	# A static coroutine: it suspends before its `Self`-typed return is produced, so the receiver has
	# to survive the suspension for the resumed frame to validate the value it comes back with.
	static func wait_for(source: Self) -> Self:
		var received: Variant = await source.done
		print("resumed parameter is Child: %s" % [received is Child])
		return source


class Child extends Base:
	pass


class Middle extends Base:
	pass


class Leaf extends Middle:
	pass


func test() -> void:
	var child := Child.new()
	child.take(Child.new())
	child.take_rest(Child.new(), Child.new())
	print("instance factory is Child: %s" % [child.make() is Child])

	# A receiver held through a base-typed local still resolves `Self` against its runtime class.
	var as_base: Base = Child.new()
	as_base.take(Child.new())
	print("base-typed receiver factory is Child: %s" % [as_base.make() is Child])

	# A receiver several levels below the declaring class resolves to its own leaf.
	var leaf := Leaf.new()
	print("three-level receiver factory is Leaf: %s" % [leaf.make() is Leaf])

	# A suspended static frame keeps the receiver it was entered with across the resumption.
	# Capture the non-void Coroutine[Child] handle so the launch is intentional, not a forgotten await.
	var _pending: Coroutine[Child] = child.wait_for(child)
	child.done.emit(child)

	# The class-handle form is unchanged.
	Child.take(Child.new())
	print("class handle factory is Child: %s" % [Child.make() is Child])
	Base.take(Base.new())
	print("base class handle factory is Base: %s" % [Base.make() is Base])
