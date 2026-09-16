trait HiddenTypes:
	class ProcessMode:
		pass

	tuple ProcessThreadGroup(int, int)


class Receiver extends Node uses HiddenTypes:
	pass


func test(receiver: Receiver) -> void:
	var mode := receiver.ProcessMode.PROCESS_MODE_INHERIT
	var group := receiver.ProcessThreadGroup.PROCESS_THREAD_GROUP_INHERIT
	print(mode, group)
