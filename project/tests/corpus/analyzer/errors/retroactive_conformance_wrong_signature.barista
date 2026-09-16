# The witness `transform` takes a `String` where the trait requires an `int`, so its signature does
# not match the required trait method.
extend RtcSignedTarget uses RtcSigned:
	func transform(value: String) -> int:
		return value.length()
