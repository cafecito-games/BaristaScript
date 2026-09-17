# A foreign class (`RtcSpeaker`) is retroactively conformed to `RtcGreeter`, supplying the required
# `loud_greet()` as a witness. The witness body calls back into `self.greet()` — a concrete method the
# target itself defines — proving a witness can invoke another method on `self` at runtime (the call
# routes through `callp` on the target instance) and read the target's own `who` member.
extend RtcSpeaker uses RtcGreeter:
	func loud_greet() -> String:
		return self.greet().to_upper()


func test() -> void:
	var s := RtcSpeaker.new()
	var g: RtcGreeter = s
	print(g.loud_greet())
	print(s.greet())
