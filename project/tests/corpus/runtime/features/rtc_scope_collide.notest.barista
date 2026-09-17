# Companion foreign target file for the witness declaration-scope collision fixture. The root class
# declares a `Marker` type and the conformance target is the inner class, so the target's own lexical
# outer scope supplies a `Marker` that has to win over the conformance file's same-named type.
class_name RtcScopeCollide
extends RefCounted


class Marker:
	func tag() -> String:
		return "target-marker"


class Inner extends RefCounted:
	var seed: int = 1
