# Edge case: a file with neither `class_name` nor `extends` has no head
# declaration that `abstract` can prefix, so a leading `abstract` here is an
# ordinary declaration modifier on the function. The implicit head class stays
# non-abstract, so declaring an abstract method in it is rejected. To make the
# head abstract you must add an explicit `abstract extends ...` (or
# `abstract class_name ...`).
abstract func describe() -> String
