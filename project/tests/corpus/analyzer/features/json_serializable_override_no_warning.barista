# `to_json` is a script-dispatched hook: a conforming class implements it and native code
# reaches the implementation through `FSJsonMarshal::call_to_json`, so declaring it must not
# report NATIVE_METHOD_OVERRIDE the way an accidental shadow of a native method would.
extends RefCounted
uses JsonSerializable

func to_json() -> JsonNode:
	return JsonNode.Str("fixture")

static func from_json(_node: JsonNode) -> JsonResult[Self]:
	return JsonResult[Self].fail("not decodable", "$")

func test():
	print(to_json())
