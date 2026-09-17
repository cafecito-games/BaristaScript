func test():
	var capture := ScriptDiagnosticCapture.new()
	capture.start()
	push_error("captured error")
	push_warning("captured warning")
	emit_fatal()
	capture.stop()

	print(capture.get_event_count())
	print(capture.has_error("captured error"))
	print(capture.has_warning("captured warning"))
	print(capture.has_fatal("captured fatal"))
	print(capture.has_error("captured fatal"))

func emit_fatal() -> void:
	push_fatal("captured fatal")
