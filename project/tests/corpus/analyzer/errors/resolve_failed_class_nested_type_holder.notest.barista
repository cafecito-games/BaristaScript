# Deliberately fails inheritance so raising this file for a nested-type lookup leaves a sticky
# analyzer error on FSParserRef — not a syntax/parser failure. Used by
# resolve_failed_class_nested_type.fs to assert the consumer diagnostic names the relationship
# instead of saying "because of a parser error", and does not cascade into a Variant nested-type error.
namespace diag.resolve_failed

class_name ResolveFailedHolder extends DoesNotExist

class Inner extends RefCounted:
	var value: int = 0
