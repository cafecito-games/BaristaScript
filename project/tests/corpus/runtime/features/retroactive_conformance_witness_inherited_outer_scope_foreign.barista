# The base outer class, the derived target, and this conformance live in three different files, so
# the inherited inner class and its lexical owner reach the compiler through cached dependency parse
# trees and separately compiled scripts. Neither the target nor this file declares `Token` or
# `TokenAlias`; both come from the outer class of the inner class the target inherits.
extend RtcInheritedOuterDerived uses InheritedOuterTaggable:
	func tag() -> String:
		return Token.new().label()

	func aliased_tag() -> String:
		return TokenAlias.new().label()


trait InheritedOuterTaggable:
	abstract func tag() -> String
	abstract func aliased_tag() -> String


func test() -> void:
	var tagged: InheritedOuterTaggable = RtcInheritedOuterDerived.new()
	print(tagged.tag())
	print(tagged.aliased_tag())
	print(RtcInheritedOuterDerived.new().from_method())
