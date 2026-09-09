# Helper trait for trait_body_declaration_error. Its requirement is missing the "abstract"
# modifier, so this file fails to parse and the trait body can never be flattened into an
# implementer. The consumer-side diagnostic must point back here.
trait_name CafecitoBadRequirement

func ping() -> int
