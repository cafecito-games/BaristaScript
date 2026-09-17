# A class that inherits an inner class also inherits that inner class's lexical outer scope, and that
# scope outranks the class's own outer scope. Both same-named `Token` declarations and both
# same-named aliases are in play, so an emitted binding that skipped the inherited outer would print
# "target-outer" instead of failing loudly. An ordinary method of the derived class resolves the same
# names through the same compiler path as the witness.
#
# The inherited inner class also extends the native `Node`, whose own constant surface defines
# `NOTIFICATION_READY`. `BaseOuter` declares a class-valued constant under that exact name, so a
# compiler that consulted the native constant surface before exhausting the inherited Foundry Script
# outer scopes would bind `NOTIFICATION_READY` to the native integer `13` instead of `BaseOuter.Token`,
# and calling `.new()` on it would fail. Foundry Script scopes must outrank the native surface here too.
class BaseOuter:
	class Token:
		func label() -> String:
			return "inherited-outer"

	const TokenAlias = Token
	const NOTIFICATION_READY = Token

	class Middle:
		class Inner extends Node:
			pass


class TargetOuter:
	class Token:
		func label() -> String:
			return "target-outer"

	const TokenAlias = Token

	class Derived extends BaseOuter.Middle.Inner:
		func from_method() -> String:
			return Token.new().label() + "/" + TokenAlias.new().label()

		func native_collision_from_method() -> String:
			return NOTIFICATION_READY.new().label()


trait Taggable:
	abstract func tag() -> String
	abstract func aliased_tag() -> String
	abstract func native_collision_tag() -> String


extend TargetOuter.Derived uses Taggable:
	func tag() -> String:
		return Token.new().label()

	func aliased_tag() -> String:
		return TokenAlias.new().label()

	func native_collision_tag() -> String:
		return NOTIFICATION_READY.new().label()


func test() -> void:
	var derived := TargetOuter.Derived.new()
	var tagged: Taggable = derived
	print(tagged.tag())
	print(tagged.aliased_tag())
	print(derived.from_method())
	print(tagged.native_collision_tag())
	print(derived.native_collision_from_method())
	derived.free()
