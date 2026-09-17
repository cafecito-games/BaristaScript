import qualified_enum.repro

func test() -> void:
	var qualified: qualified_enum.repro.Status = qualified_enum.repro.Status.READY
	print(qualified.label())
	print(qualified_enum.repro.Status.parse("busy").label())

	var imported: Status = Status.READY
	print(imported.label())
	print(Status.parse("busy").label())
