final class_name UnionPayloadNestedHolder extends RefCounted uses UnionPayloadNestedMarker

final class Inner extends RefCounted:
	var value: int = 0

enum Tier:
	LOW = 0
	HIGH = 1

# The member that closes the cycle: the holder names the union whose payload names a type
# nested in the holder itself.
var pick: UnionPayloadNestedPick? = null

func tag() -> int:
	return 1
