# A bare type parameter has no meaning as a union alternative, and `Self` is no exception. The report
# renders the annotation the author wrote rather than the internal parameter name.
class Receiver:
	var link: int | Self = 0


func test() -> void:
	print(Receiver.new().link)
