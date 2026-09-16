# The witness signature matches the trait requirement, but its body references an undeclared
# identifier. Witness bodies are analyzed in the target's context, so the body error is reported.
extend RtcBodyTarget uses RtcBodyTrait:
	func describe() -> String:
		return undeclared_value
