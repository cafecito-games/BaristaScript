# When a dependency cannot be raised, a nested type under that class must report one root-cause
# diagnostic that names the resolving type — not a false "parser error" plus a cascaded
# "nested type under Variant" follow-on.
namespace diag.resolve_failed

enum_name ResolveFailedPick:
	Inner(inner: ResolveFailedHolder.Inner)
	Text(text: String)
