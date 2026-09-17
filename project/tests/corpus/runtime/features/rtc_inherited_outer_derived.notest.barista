# Companion conformance target. It inherits an inner class of another file and declares nothing of
# its own, so every name below has to come from the inherited inner class's lexical outer scope.
class_name RtcInheritedOuterDerived
extends RtcInheritedOuterBase.Middle.Inner


func from_method() -> String:
	return Token.new().label() + "/" + TokenAlias.new().label()
