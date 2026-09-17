# Companion trait for the native `Self`-in-a-generic fixture. The requirement mentions `Self` as a
# type argument of a generic rather than on its own, which is the shape `JsonSerializable.from_json`
# uses and the shape that has to survive compiled-bytecode export.
trait_name RtcNativeSelfy

abstract static func wrapped() -> JsonResult[Self]
