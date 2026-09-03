# `abstract` may prefix a top-level `extends` (without `class_name`), marking the
# otherwise-unnamed head class abstract. Because the head is abstract, declaring
# an abstract method in it is accepted; without the prefix the head would be a
# concrete class and this would be a "not abstract but contains abstract methods"
# error (see analyzer/errors/abstract_method_in_non_abstract_head.fs).
abstract extends RefCounted

abstract func describe() -> String
