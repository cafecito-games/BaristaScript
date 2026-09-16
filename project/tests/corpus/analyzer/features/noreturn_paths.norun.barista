@noreturn
func abort_user() -> void:
	push_fatal("abort")

func returns_from_push_fatal() -> int:
	push_fatal("not implemented")

func returns_from_user_noreturn() -> int:
	abort_user()

func returns_from_if(flag: bool) -> int:
	if flag:
		return 1
	else:
		abort_user()

func returns_from_all_if_noreturn(flag: bool) -> int:
	if flag:
		abort_user()
	else:
		push_fatal("stop")

func returns_from_match(value: int) -> String:
	match value:
		0:
			abort_user()
		_:
			push_fatal("stop")

func returns_from_while_true() -> int:
	while true:
		abort_user()
