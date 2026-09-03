# Unnamed tuple types `(T, ...)` are accepted anywhere a type appears: variable
# annotations, parameter types, and return types. Static tuple typing (element-wise
# compatibility) is a follow-up change; this only exercises the syntax.
var position: (int, int)

func make_pair() -> (int, String):
	return (1, "one")

func consume_pair(pair: (int, String)) -> void:
	pass
