# This file plays the role of the derived target's own declaring file: it inherits a foreign base's
# inner class through `rtc_inherited_outer_base.notest.fs`, which this compilation unit only holds
# shallowly, so the foreign `TokenAlias` constant has no entry in any populated constant pool. This
# file also declares its own, lower-precedence `TokenAlias` naming a different class. A name-only
# scope walk that skips the foreign scope's empty pool would otherwise find this later declaration
# instead of the one analysis selected. Both a conformance witness and an ordinary method on the
# derived class must still resolve `TokenAlias` to the inherited foreign declaration.
class WrongToken:
	func label() -> String:
		return "target-outer"


const TokenAlias = WrongToken


class RtcAliasCollisionDerived extends RtcInheritedOuterBase.Middle.Inner:
	func ordinary_tag() -> String:
		return TokenAlias.new().label()


trait InheritedOuterAliasCollisionTaggable:
	abstract func tag() -> String


extend RtcAliasCollisionDerived uses InheritedOuterAliasCollisionTaggable:
	func tag() -> String:
		return TokenAlias.new().label()


func test() -> void:
	var tagged: InheritedOuterAliasCollisionTaggable = RtcAliasCollisionDerived.new()
	print(tagged.tag())
	print(RtcAliasCollisionDerived.new().ordinary_tag())
