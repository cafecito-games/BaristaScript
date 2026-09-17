# Companion base graph for the inherited-outer-scope fixtures. The nested class and the alias a
# derived class reaches live two lexical hops above the inner class it inherits, in a different file
# from both the derived class and the `extend` that conforms it.
class_name RtcInheritedOuterBase
extends RefCounted


class Token:
	func label() -> String:
		return "inherited-outer"


const TokenAlias = Token


class Middle:
	class Inner:
		pass
