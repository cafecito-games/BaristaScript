# Companion foreign target class for the witness-calls-self-method runtime fixture. A separate
# `extend` retroactively conforms it to RtcGreeter; the witness reads `who` and calls `greet()`, both of
# which belong to this class's own surface.
class_name RtcSpeaker
extends RefCounted

var who: String = "world"


func greet() -> String:
	return "hi " + who
