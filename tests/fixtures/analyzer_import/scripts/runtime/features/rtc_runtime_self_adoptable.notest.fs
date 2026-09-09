# Companion trait for the instance-receiver static-witness fixture. Its single requirement is a
# *static* method whose signature is written in terms of `Self`, so dispatching it needs a receiver
# to resolve `Self` against no matter which form the caller reaches it through.
trait_name RtcSelfAdoptable

abstract static func adopt(value: Self) -> Self
