# A `final` class has no subtypes, so a method the class does not declare can never be supplied by
# something the analyzer cannot see. The call below used to pass analysis in silence — the
# `UNSAFE_METHOD_ACCESS` warning that covered it is ignored by default — and failed only when the
# line ran.
final class FrmMessage:
	func send() -> void:
		pass


func test() -> void:
	var message := FrmMessage.new()
	message.deliver()
