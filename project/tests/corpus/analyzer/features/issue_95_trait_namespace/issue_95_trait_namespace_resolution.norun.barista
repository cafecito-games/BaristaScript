namespace issue_95.combat
import issue_95.shared

extends RefCounted
uses Issue95Damageable, Issue95Loggable, controllers.Issue95Trackable

var same_namespace: Issue95Damageable
var imported_short: Issue95Loggable
var fully_qualified: issue_95.shared.Issue95Loggable
var child_namespace: controllers.Issue95Trackable
var fully_qualified_child: issue_95.combat.controllers.Issue95Trackable
var typed_container: Array[Issue95Damageable] = []
var qualified_container: Array[issue_95.shared.Issue95Loggable] = []

func require_damageable(_value: Issue95Damageable) -> void:
	pass

func require_loggable(_value: issue_95.shared.Issue95Loggable) -> void:
	pass

func provide_damageable() -> Issue95Damageable:
	return null

func provide_loggable() -> issue_95.shared.Issue95Loggable:
	return null

func require_trackable(_value: controllers.Issue95Trackable) -> void:
	pass

func test() -> void:
	var value: Variant = self
	if value is Issue95Damageable:
		var _typed: Issue95Damageable = value
	var _cast := value as issue_95.shared.Issue95Loggable
	take_damage(5)
	record()
