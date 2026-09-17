# The conformance target for the namespace-reach fixtures. It is an ordinary global class in no
# namespace, and knows nothing about `Gadgetlike`: the witness is supplied externally by
# `rtc_ns_conformance.notest.fs` and reads this member through `self`. Keeping the target and the
# trait out of `rtc_ns` is what makes those fixtures test the namespace edge — a consumer must not be
# able to pick the conformance up as a side effect of loading some *other* file in that namespace.
final class_name RtcNsWidget extends RefCounted

var label: String = "widget"
