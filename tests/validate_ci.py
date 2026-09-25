#!/usr/bin/env python3
# validate_ci.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
from build_config import load_config, api_path, validate_api_file, require_equal
from corpus_registry import validate_registration  # noqa: E402

SUITE_RUNNER = "tests/run_gdscript_suites.py"

BASELINE_PATH = ROOT / "tests" / "corpus_baseline.json"
SUITES_MANIFEST_PATH = ROOT / "tests" / "gdscript_suites.json"

# Godot's `--script`, its documented `-s` alias, and the quoting a workflow author may add.
DIRECT_SUITE_INVOCATION = re.compile(r"""(?:--script|(?<![\w-])-s)\s+['"]?res://\S+""")

# A `#` that starts a line or follows whitespace begins a comment, in YAML and in the shell
# blocks the workflow embeds.
COMMENT_TAIL = re.compile(r"(?:^|(?<=\s))#.*$")

# The YAML scaffolding around an embedded shell command: the list dash and the `run:` key.
COMMAND_LINE_PREFIX = re.compile(r"^\s*(?:-\s*)?(?:run:\s*[|>]?[-+]?\s*)?")

# Where one shell command ends and the next begins, with the operator kept.
COMMAND_SEPARATOR = re.compile(r"(\|\||&&|[;|])")

# `|| true`, `; true` and friends turn the runner's non-zero exit into a green step.
SUPPRESSED_STATUS = re.compile(r"\|\||;\s*true\b|&&\s*true\b")

CONSTANT_FALSE_EXPRESSION = re.compile(r"\$\{\{\s*false\s*\}\}")


def executable_lines(command: str) -> list[str]:
    """A run scalar's lines with whole-line and inline shell comments removed.

    A commented-out command is text a substring search would still find, so the
    wiring checks below must not read one as evidence that CI runs anything.
    """
    return [COMMENT_TAIL.sub("", line) for line in command.splitlines()]


def line_invocations(line: str, runner_path: str = SUITE_RUNNER) -> list[str]:
    """The text following each command in `line` that actually runs the suite runner.

    The returned text is the rest of the shell line, so a caller can see both the
    runner's own arguments and whatever the line does with its exit status.
    """
    runner_command = re.compile(
        r"""^(?:\w+=\S*\s+)*(?:python3?|py)\s+['"]?[^\s'"]*"""
        + re.escape(runner_path)
        + r"""(?P<arguments>\s.*|$)"""
    )
    body = COMMAND_LINE_PREFIX.sub("", line)
    pieces = COMMAND_SEPARATOR.split(body)
    invocations: list[str] = []
    for index in range(0, len(pieces), 2):
        match = runner_command.match(pieces[index].strip())
        if match is not None:
            invocations.append(match.group("arguments") + "".join(pieces[index + 1 :]))
    return invocations


def workflow_steps(workflow: str) -> list[tuple[dict, dict]]:
    """Return each parsed step together with its enclosing job mapping."""
    try:
        import yaml
    except ImportError as error:
        raise ValueError(
            "CI audit requires PyYAML: python3 -m pip install -r tests/requirements.txt"
        ) from error

    try:
        document = yaml.load(workflow, Loader=yaml.BaseLoader)
    except yaml.YAMLError as error:
        raise ValueError(f"invalid CI YAML: {error}") from error
    if not isinstance(document, dict):
        raise ValueError("CI workflow must be a mapping")
    jobs = document.get("jobs")
    if not isinstance(jobs, dict):
        raise ValueError("CI jobs must be a mapping")

    parsed: list[tuple[dict, dict]] = []
    for job in jobs.values():
        if not isinstance(job, dict) or "steps" not in job:
            continue
        steps = job["steps"]
        if not isinstance(steps, list):
            raise ValueError("CI job steps must be a list")
        parsed.extend((job, step) for step in steps if isinstance(step, dict))
    return parsed


def runner_invocations(
    job_steps: list[tuple[dict, dict]], runner_path: str
) -> list[tuple[str, dict, dict]]:
    """Every suite-runner command, paired with its parsed job and step."""
    invocations: list[tuple[str, dict, dict]] = []
    for job, step in job_steps:
        command = step.get("run")
        if not isinstance(command, str):
            continue
        for line in executable_lines(command):
            for arguments in line_invocations(line, runner_path):
                invocations.append((arguments, job, step))
    return invocations


def condition_is_unreachable(owner: dict) -> bool:
    """Whether a job or step condition is exactly a normalized constant false."""
    condition = owner.get("if")
    if not isinstance(condition, str):
        return False
    condition = condition.strip()
    return condition == "false" or CONSTANT_FALSE_EXPRESSION.fullmatch(condition) is not None


def step_continues_on_error(step: dict) -> bool:
    """Match the existing policy: only a literal false preserves failure propagation."""
    value = step.get("continue-on-error", "false")
    return not isinstance(value, str) or value.strip() != "false"


def check_suite_runner_wiring(
    workflow: str, runner_path: str, suite_name: str, require_whole_manifest: bool = False
) -> str | None:
    """Require a reachable runner step whose failure can make the job fail."""
    try:
        job_steps = workflow_steps(workflow)
    except ValueError as error:
        return str(error)

    invocations = [
        (arguments, job, step)
        for arguments, job, step in runner_invocations(job_steps, runner_path)
        if "--godot" in arguments and "--list" not in arguments
    ]
    if not invocations:
        return (
            f"CI must run the {suite_name} suites through {runner_path} --godot <binary>; "
            "naming the runner, or running it in --list mode, launches no suite"
        )

    effective = [
        arguments
        for arguments, job, step in invocations
        if not SUPPRESSED_STATUS.search(arguments)
        and not step_continues_on_error(step)
        and not condition_is_unreachable(job)
        and not condition_is_unreachable(step)
    ]
    if not effective:
        return (
            f"CI must let {runner_path} fail the job from a potentially reachable job and step; "
            "its exit status is the guard, so no effective invocation may be suppressed, "
            "continue on error, or use a constant-false condition"
        )
    if require_whole_manifest and not any(
        not re.search(r"--(?:suite|case)\b", arguments) for arguments in effective
    ):
        return "CI must execute the complete native suite manifest without --suite/--case filters"
    return None


def check_gdscript_suite_wiring(workflow: str) -> str | None:
    """Return a complaint when the workflow could run a GDScript suite unguarded.

    A GDScript parse error makes SceneTree quit 0, so a suite invoked straight
    from the workflow can stop testing while the job stays green. Every suite
    must go through tests/run_gdscript_suites.py, which demands the guard
    sentinel that only an executed suite can print, and the runner's own exit
    status must be allowed to fail the job.
    """
    try:
        job_steps = workflow_steps(workflow)
    except ValueError as error:
        return str(error)
    active = "\n".join(
        line
        for _job, step in job_steps
        if isinstance(step.get("run"), str)
        for line in executable_lines(step["run"])
    )

    direct_invocations = DIRECT_SUITE_INVOCATION.findall(active)
    if direct_invocations:
        return (
            "CI must not invoke a GDScript suite directly "
            f"({', '.join(sorted(set(direct_invocations)))}); "
            f"run it through {SUITE_RUNNER} so a parse error cannot pass as green"
        )

    return check_suite_runner_wiring(workflow, SUITE_RUNNER, "GDScript")


def check_native_suite_wiring(workflow: str) -> str | None:
    """Require a reachable unsuppressed execution of the complete native manifest."""
    return check_suite_runner_wiring(
        workflow, "tests/run_native_suites.py", "native", require_whole_manifest=True
    )


def check_corpus_baseline() -> str | None:
    """Return a complaint when the corpus baseline, the tree and the CI pin disagree.

    The baseline is the one committed number, and it is only worth committing if
    every way of losing cases is a failure. Three things have to agree: how many
    case files are on disk, what tests/corpus_baseline.json records, and the
    anchored summary line tests/gdscript_suites.json pins. Cases quietly
    vanishing then fails on the first, a case quietly starting to fail fails on
    the third, and a case the baseline records as expected-fail that starts
    passing fails on the third too -- the pinned pass count is exact, so drift in
    either direction is a red build.

    The triage ledger additionally proves that every upstream runnable case is
    either imported or has exactly one path-specific excluded/deferred
    disposition, and that every rewrite/expectation override names an imported
    case with a non-empty reason.
    """
    try:
        validate_registration(ROOT, baseline_path=BASELINE_PATH, suites_path=SUITES_MANIFEST_PATH)
    except (ValueError, OSError) as error:
        return str(error)
    return None


def check_corpus_reproducibility_wiring(workflow: str) -> str | None:
    """Audit the executable job structure, including acquisition and failure propagation.

    BaseLoader retains YAML scalar spellings (not YAML 1.1's boolean `on`).
    A narrow command allowlist makes shell wrappers, mutable refs and hidden
    status suppression reviewable changes instead of substring matches.
    """
    try:
        import yaml
    except ImportError:
        return "CI audit requires PyYAML: python3 -m pip install -r tests/requirements.txt"

    class UniqueLoader(yaml.BaseLoader):
        def construct_mapping(self, node, deep=False):
            result = {}
            for key_node, value_node in node.value:
                key = self.construct_object(key_node, deep=deep)
                if not isinstance(key, str) or key in result:
                    raise ValueError("duplicate or non-string YAML mapping key")
                result[key] = self.construct_object(value_node, deep=deep)
            return result

    try:
        document = yaml.load(workflow, Loader=UniqueLoader)
        if not isinstance(document, dict):
            return "CI workflow must be a mapping"
        events = document.get("on")
        if (not isinstance(events, dict)
                or set(events) != {"push", "pull_request", "merge_group", "workflow_call"}
                or events["push"] != {"branches": ["main"]}
                or any(events[event] not in ("", None, {})
                       for event in ("pull_request", "merge_group", "workflow_call"))):
            return "CI must preserve main push, pull_request, merge_group and workflow_call events without filters"
        if "defaults" in document or "env" in document:
            return "corpus-reproducibility must not inherit workflow shell or environment overrides"
        jobs = document.get("jobs", {})
        if not isinstance(jobs, dict):
            return "CI jobs must be a mapping"
        job = jobs.get("corpus-reproducibility")
        if (not isinstance(job, dict) or set(job) - {"name", "runs-on", "permissions", "steps"}
                or job.get("name", "corpus-reproducibility") != "corpus-reproducibility"
                or job.get("runs-on") != "ubuntu-22.04"
                or job.get("permissions") != {"contents": "read"}):
            return "CI requires one unsuppressed Linux corpus-reproducibility job outside the matrix"
        steps = job.get("steps")
        if not isinstance(steps, list) or len(steps) != 8:
            return "corpus-reproducibility requires checkout, Python setup, dependency, outputs, upstream checkout, tests, corpus check and complete inventory steps"
        expected = [
            {"uses": "actions/checkout@v4", "with": {"persist-credentials": "false", "submodules": "false", "fetch-depth": "0"}},
            {"uses": "actions/setup-python@v5", "with": {"python-version": "3.x"}},
            {"shell": "bash", "run": "python3 -m pip install -r tests/requirements.txt"},
            {"id": "source", "shell": "bash", "run": 'python3 scripts/check_corpus_reproducibility.py --github-output "$GITHUB_OUTPUT"'},
            {"uses": "actions/checkout@v4", "with": {
                "repository": "${{ steps.source.outputs.repository }}",
                "ref": "${{ steps.source.outputs.revision }}",
                "path": ".upstream-foundry", "submodules": "false",
                "persist-credentials": "false", "fetch-depth": "1",
                "sparse-checkout-cone-mode": "false",
                "sparse-checkout": "${{ steps.source.outputs.sparse_paths }}"}},
            {"shell": "bash", "run": "python3 tests/test_corpus_reproducibility.py --foundry .upstream-foundry\npython3 tests/test_import_analyzer_corpus.py --foundry .upstream-foundry\npython3 tests/test_import_runtime_corpus.py --foundry .upstream-foundry\npython3 tests/test_corpus_ratchet.py"},
            {"shell": "bash", "run": "python3 scripts/check_corpus_reproducibility.py --foundry .upstream-foundry"},
            {"shell": "bash", "run": "python3 tests/validate_analyzer_port_inventory.py --foundry-dir .upstream-foundry --require-m3-complete\npython3 tests/test_analyzer_port_inventory.py --foundry-dir .upstream-foundry"},
        ]
        for index, (step, required) in enumerate(zip(steps, expected), 1):
            if not isinstance(step, dict):
                return f"corpus-reproducibility step {index} must be a mapping"
            actual = {key: value.strip() if key == "run" and isinstance(value, str) else value
                      for key, value in step.items() if key != "name"}
            if actual != required:
                return f"corpus-reproducibility step {index} must retain its validated inputs and unsuppressed command"
        # The build matrix validates before setup-godot-cpp. Provision Python
        # here too: hosted macOS's system/Homebrew pip is externally managed.
        build_steps = jobs.get("build", {}).get("steps", [])
        build_prefix = [
            {"uses": "actions/setup-python@v5", "with": {"python-version": "3.x"}},
            {"shell": "bash", "run": "\n".join((
                "python3 -m pip install -r tests/requirements.txt",
                "python3 tests/validate_ci.py",
                "python3 tests/test_native_transition_scope.py",
                "python3 tests/test_build_config.py",
                "python3 tests/test_build_consumers.py",
                "python3 tests/test_query_build_info.py",
                "python3 tests/test_run_gdscript_suites.py",
                "python3 tests/test_corpus_baseline.py",
                "python3 tests/test_corpus_expectations.py"))},
        ]
        if not isinstance(build_steps, list) or len(build_steps) < 3:
            return "build matrix requires setup-managed Python before offline validation"
        for step, required in zip(build_steps[1:3], build_prefix):
            if not isinstance(step, dict):
                return "build matrix Python setup and validation steps must be mappings"
            actual = {key: value.strip() if key == "run" and isinstance(value, str) else value
                      for key, value in step.items() if key != "name"}
            if actual != required:
                return "build matrix requires unsuppressed setup-managed Python before offline validation"
        # Naming this command in another job would duplicate the gate per matrix
        # entry; only the dedicated job owns upstream acquisition/checking.
        for name, other in jobs.items():
            if name != "corpus-reproducibility" and isinstance(other, dict):
                if any(isinstance(step, dict) and "scripts/check_corpus_reproducibility.py" in step.get("run", "")
                       for step in other.get("steps", [])):
                    return "corpus reproducibility must run only in its dedicated job"
    except (yaml.YAMLError, ValueError, TypeError) as error:
        return f"invalid CI YAML: {error}"
    return None


def corpus_triage_step(corpus: str) -> dict:
    """The pinned triage invocation for one corpus.

    Both corpora are gated by the same command with one argument changed, so they are derived
    from one shape rather than written twice: a change to the execution mode, the shard count
    or the timeout has to be made once and then holds for every corpus, and a corpus quietly
    gated more weakly than its sibling is not expressible.
    """
    return {
        "if": "${{ matrix.target.platform == 'linux' && matrix.target.arch == 'x86_64' && matrix.target-type == 'template_debug' }}",
        "shell": "bash",
        "env": {"GODOT_VERSION": "${{ steps.versions.outputs.godot_runtime }}"},
        "run": 'python3 scripts/run_corpus_triage.py'
               ' --godot "$RUNNER_TEMP/godot/Godot_v${GODOT_VERSION}-stable_linux.x86_64"'
               f' --corpus res://tests/corpus/{corpus}'
               f' --report "$RUNNER_TEMP/{corpus}-corpus-triage.json"'
               ' --execution fast'
               # One shard per vCPU on the GitHub-hosted x86_64 Linux runner. Each shard is a
               # single-threaded, CPU-bound Godot process, so more shards than cores would only
               # oversubscribe and fewer would leave the runner idle.
               ' --shards 4'
               ' --timeout 900',
    }


# The triage-supervised corpora, in the order CI runs them. The analyzer corpus runs first
# because it is the cheaper gate and a front-end regression explains most runtime failures.
TRIAGE_CORPORA = ("analyzer", "runtime")
CORPUS_TRIAGE_STEPS = [corpus_triage_step(corpus) for corpus in TRIAGE_CORPORA]


LINUX_VERIFICATION_CONDITION = (
    "${{ matrix.target.platform == 'linux' && matrix.target.arch == 'x86_64' "
    "&& matrix.target-type == 'template_debug' }}"
)

LINUX_GODOT_BINARY = 'godot_binary="$RUNNER_TEMP/godot/Godot_v${GODOT_VERSION}-stable_linux.x86_64"'


# The SCons build-system transition step runs on a pull request only when the scope decision is
# not an explicit 'false', and on every other event regardless of it. See
# scripts/native_transition_scope.py for why the build inputs it names are sufficient.
LINUX_TRANSITION_CONDITION = (
    "${{ matrix.target.platform == 'linux' && matrix.target.arch == 'x86_64' "
    "&& matrix.target-type == 'template_debug' "
    "&& (github.event_name != 'pull_request' || steps.transition_scope.outputs.required != 'false') }}"
)

# The CMake verification has its own checkout and scope decision so it can run in parallel with
# the matrix Linux job. It has no matrix selectors, but otherwise fails open in exactly the same
# way as the SCons transition step.
CMAKE_TRANSITION_CONDITION = (
    "${{ github.event_name != 'pull_request' "
    "|| steps.transition_scope.outputs.required != 'false' }}"
)


def linux_verification_step(name: str, *commands: str, condition: str = LINUX_VERIFICATION_CONDITION) -> dict:
    return {
        "name": name,
        "if": condition,
        "shell": "bash",
        "env": {"GODOT_VERSION": "${{ steps.versions.outputs.godot_runtime }}"},
        "run": "\n".join(commands),
    }


# The Linux debug verification runs as separately named steps so each phase's duration is
# visible in the Actions UI, which is why the names are pinned along with the commands. Steps
# share no shell state: the first downloads Godot, and every later one derives the same binary
# path itself. The matrix sequence builds on the SCons test library while the independent CMake
# job owns its ON, OFF and ON build tree, so order within each job is part of the pin as much as
# the commands are.
#
# The step directly before the sequence. Pinning it bounds the sequence at its start as the
# triage step bounds it at its end, so nothing unpinned can run between the two.
LINUX_VERIFICATION_PREDECESSOR = {
    "name": "Probe the removed-warning-code compile error",
    "if": LINUX_VERIFICATION_CONDITION,
    "shell": "bash",
    "run": "python3 tests/test_warning_code_removal.py",
}

# The decision the transition steps read. Every other event and every undecidable pull request
# must still run them, which the step cannot express on its own: check_native_transition_scope
# pins the decision the script actually makes.
LINUX_TRANSITION_SCOPE_STEP = {
    "name": "Decide whether to verify native build-system transitions",
    "id": "transition_scope",
    "if": LINUX_VERIFICATION_CONDITION,
    "shell": "bash",
    "env": {
        "EVENT_NAME": "${{ github.event_name }}",
        "PULL_REQUEST_HEAD": "${{ github.event.pull_request.head.sha }}",
    },
    "run": 'python3 scripts/native_transition_scope.py --event "$EVENT_NAME"'
           ' --pull-request-head "$PULL_REQUEST_HEAD" --github-output "$GITHUB_OUTPUT"',
}

CMAKE_TRANSITION_SCOPE_STEP = {
    key: value for key, value in LINUX_TRANSITION_SCOPE_STEP.items() if key != "if"
}

LINUX_VERIFICATION_STEPS = [
    linux_verification_step(
        "Verify editor recognition and GDScript suites",
        "curl --fail --location --retry 3 \\",
        '  --output "$RUNNER_TEMP/godot.zip" \\',
        '  "https://github.com/godotengine/godot-builds/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_linux.x86_64.zip"',
        'unzip -q "$RUNNER_TEMP/godot.zip" -d "$RUNNER_TEMP/godot"',
        LINUX_GODOT_BINARY,
        'chmod +x "$godot_binary"',
        "",
        '"$godot_binary" --headless --path project --editor --quit',
        "grep -q 'example.barista::BaristaScript' project/.godot/editor/filesystem_cache*",
        'python3 tests/run_gdscript_suites.py --godot "$godot_binary"',
    ),
    linux_verification_step(
        "Run the committed runtime fixture on stock Godot",
        LINUX_GODOT_BINARY,
        'python3 tests/test_runtime_scene.py --godot "$godot_binary"',
    ),
    linux_verification_step(
        "Build and run the native suites with SCons",
        LINUX_GODOT_BINARY,
        "scons api_version=${{ steps.versions.outputs.godot_api }} target=template_debug barista_tests=yes",
        'python3 tests/test_run_native_suites.py --godot "$godot_binary"',
        'python3 tests/test_native_storage.py --godot "$godot_binary"',
        'python3 tests/run_native_suites.py --godot "$godot_binary"',
        'python3 tests/test_run_corpus_triage.py --godot "$godot_binary" \\',
        '  --library "$(python3 -c "import json; print(json.load(open(\'build/native-scons/native-artifact.json\'))[\'library\'])")"',
    ),
    LINUX_TRANSITION_SCOPE_STEP,
    linux_verification_step(
        "Verify the SCons native test off/on transition",
        LINUX_GODOT_BINARY,
        "scons api_version=${{ steps.versions.outputs.godot_api }} target=template_debug barista_tests=no",
        "python3 tests/verify_native_surface.py --binary-dir project/bin/linux --target-type template_debug",
        "scons api_version=${{ steps.versions.outputs.godot_api }} target=template_release barista_tests=no",
        "python3 tests/verify_native_surface.py --binary-dir project/bin/linux --target-type template_release",
        "scons api_version=${{ steps.versions.outputs.godot_api }} target=template_debug barista_tests=yes",
        'python3 tests/run_native_suites.py --godot "$godot_binary"',
        condition=LINUX_TRANSITION_CONDITION,
    ),
]


def cmake_verification_step(name: str, *commands: str) -> dict:
    return {
        "name": name,
        "if": CMAKE_TRANSITION_CONDITION,
        "shell": "bash",
        "env": {"GODOT_VERSION": "${{ steps.versions.outputs.godot_runtime }}"},
        "run": "\n".join(commands),
    }


CMAKE_VERIFICATION_STEPS = [
    cmake_verification_step(
        "Build and run the native suites with CMake",
        LINUX_GODOT_BINARY,
        "cmake -S . -B build/native-cmake -DCMAKE_BUILD_TYPE=Debug -DBARISTA_TESTS=ON",
        "cmake --build build/native-cmake --parallel 4",
        'python3 tests/test_run_native_suites.py --godot "$godot_binary" --build-dir build/native-cmake',
        'python3 tests/test_native_storage.py --godot "$godot_binary" --build-dir build/native-cmake',
        'python3 tests/run_native_suites.py --godot "$godot_binary" --build-dir build/native-cmake',
    ),
    cmake_verification_step(
        "Verify incremental native CMake rebuilds",
        LINUX_GODOT_BINARY,
        'python3 tests/test_native_cmake_rebuild.py --godot "$godot_binary" --build-dir build/native-cmake --jobs 4',
    ),
    cmake_verification_step(
        "Verify the CMake native test off/on transition",
        LINUX_GODOT_BINARY,
        "cmake -S . -B build/native-cmake -DBARISTA_TESTS=OFF",
        "cmake --build build/native-cmake --parallel 4",
        "python3 tests/verify_native_surface.py --binary-dir project/bin/linux --target-type template_debug",
        "cmake -S . -B build/native-cmake -DBARISTA_TESTS=ON",
        "cmake --build build/native-cmake --parallel 4",
        'python3 tests/run_native_suites.py --godot "$godot_binary" --build-dir build/native-cmake',
    ),
]

CMAKE_VERIFICATION_PREFIX = [
    {
        "name": "Checkout BaristaScript",
        "uses": "actions/checkout@v4",
        "with": {"persist-credentials": "false", "submodules": "true"},
    },
    {
        "name": "Setup Python for native CMake verification",
        "uses": "actions/setup-python@v5",
        "with": {"python-version": "3.x"},
    },
    CMAKE_TRANSITION_SCOPE_STEP,
    {
        "name": "Resolve shared build versions",
        "id": "versions",
        "if": CMAKE_TRANSITION_CONDITION,
        "shell": "bash",
        "run": 'python3 scripts/build_config.py --platform linux --format github >> "$GITHUB_OUTPUT"',
    },
    cmake_verification_step(
        "Download stock Godot",
        "curl --fail --location --retry 3 \\",
        '  --output "$RUNNER_TEMP/godot.zip" \\',
        '  "https://github.com/godotengine/godot-builds/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_linux.x86_64.zip"',
        'unzip -q "$RUNNER_TEMP/godot.zip" -d "$RUNNER_TEMP/godot"',
        LINUX_GODOT_BINARY,
        'chmod +x "$godot_binary"',
    ),
]


def check_linux_verification_wiring(workflow: str) -> str | None:
    """Require the parallel Linux verification jobs, with every claim exactly pinned.

    The general runner audits accept any one reachable invocation, so on their own they would
    not notice a build, a surface check or a sweep being dropped from this sequence. The steps
    are compared whole, display names included and only surrounding whitespace ignored. They
    must sit directly after the warning-code probe and directly before the corpus triage steps.
    The serial CMake chain must live in an independent, dependency-free job so it overlaps the
    matrix job instead of extending its critical path.
    """
    try:
        import yaml
    except ImportError:
        return "CI audit requires PyYAML: python3 -m pip install -r tests/requirements.txt"
    try:
        document = yaml.load(workflow, Loader=yaml.BaseLoader)
        jobs = document["jobs"]
        job = jobs["build"]
        steps = job["steps"]
        if not isinstance(steps, list):
            return "build job steps must be a list"
        cmake_job = jobs["native-cmake-verification"]
        cmake_steps = cmake_job["steps"]
        if not isinstance(cmake_steps, list):
            return "native-cmake-verification job steps must be a list"
    except (yaml.YAMLError, KeyError, TypeError) as error:
        return f"invalid CI YAML: {error}"

    triage = [
        index
        for index, step in enumerate(steps)
        if isinstance(step, dict)
        and isinstance(step.get("run"), str)
        and "scripts/run_corpus_triage.py" in step["run"]
    ]
    # The triage steps are required to be contiguous and to come last, so "directly before"
    # stays a statement about one boundary rather than about each corpus separately.
    contiguous = triage == list(range(triage[0], triage[0] + len(CORPUS_TRIAGE_STEPS))) if triage else False
    if not contiguous or triage[0] <= len(LINUX_VERIFICATION_STEPS):
        return (
            "the build job must run its Linux verification steps directly before the "
            "contiguous corpus triage steps"
        )
    first = triage[0] - len(LINUX_VERIFICATION_STEPS)
    predecessor = steps[first - 1]
    if not isinstance(predecessor, dict) or {
        key: value.strip() if key == "run" and isinstance(value, str) else value
        for key, value in predecessor.items()
    } != LINUX_VERIFICATION_PREDECESSOR:
        return (
            "the Linux verification steps must follow the removed-warning-code probe directly, "
            "with no unpinned step before them"
        )
    for offset, required in enumerate(LINUX_VERIFICATION_STEPS):
        step = steps[first + offset]
        if not isinstance(step, dict):
            return f"Linux verification step {offset + 1} must be a mapping"
        actual = {
            key: value.strip() if key == "run" and isinstance(value, str) else value
            for key, value in step.items()
        }
        if actual != required:
            return (
                f"Linux verification step {offset + 1} must retain its name, validated inputs and "
                "exact unsuppressed commands, in order, directly before the corpus triage step"
            )
    if condition_is_unreachable(job):
        return "the Linux verification steps must be able to fail the build job"
    if (
        set(cmake_job) != {"runs-on", "permissions", "steps"}
        or cmake_job.get("runs-on") != "ubuntu-22.04"
        or cmake_job.get("permissions") != {"contents": "read"}
        or condition_is_unreachable(cmake_job)
    ):
        return (
            "native-cmake-verification must be an independent, unsuppressed Ubuntu job with "
            "read-only repository access"
        )
    required_cmake_steps = CMAKE_VERIFICATION_PREFIX + CMAKE_VERIFICATION_STEPS
    if len(cmake_steps) != len(required_cmake_steps):
        return "native-cmake-verification must retain its complete pinned setup and verification"
    for offset, required in enumerate(required_cmake_steps):
        step = cmake_steps[offset]
        if not isinstance(step, dict):
            return f"native CMake verification step {offset + 1} must be a mapping"
        actual = {
            key: value.strip() if key == "run" and isinstance(value, str) else value
            for key, value in step.items()
        }
        if actual != required:
            return (
                f"native CMake verification step {offset + 1} must retain its name, validated "
                "inputs and exact unsuppressed command"
            )
    return None


# Paths whose change must run the build-system transitions. The first group is where the test
# option and the vendored test framework live; the rest decide where each build mode leaves its
# state, judge the transitions, or are the gate itself. A directory is represented by a file
# inside it, including one that does not exist yet. `tests/native/CMakeLists.txt` and
# `tests/native/SCsub` are here to pin that narrowing the directory out of the classifier left
# the repository-wide build-description rules covering it; the case files that used to sit in
# this list are in ORDINARY_TRANSITION_PATHS below for the matching reason.
REQUIRED_TRANSITION_INPUTS = (
    "SConstruct",
    "CMakeLists.txt",
    "methods.py",
    "scripts/native_test_build.py",
    "build_profile.json",
    "build_versions.json",
    "tests/native/CMakeLists.txt",
    "tests/native/SCsub",
    ".github/workflows/ci.yml",
    "thirdparty/doctest/doctest.h",
    "godot-cpp",
    ".gitmodules",
    "custom.py",
    ".github/actions/setup-godot-cpp/action.yml",
    "scripts/build_config.py",
    "scripts/build_metadata.py",
    "scripts/generate_global_api.py",
    "scripts/native_transition_scope.py",
    "tests/verify_native_surface.py",
    "tests/verify_parse_cache_surface.py",
    "tests/test_native_cmake_rebuild.py",
    "tests/test_cmake_api_inputs.py",
    "tests/run_native_suites.py",
    "tests/native_suites.json",
    "tests/test_native_storage.py",
    "tests/test_run_native_suites.py",
    "cmake/toolchain.txt",
    "src/SCsub",
    "tools/SConscript",
    "tools/extension.cmake",
)

# Paths a pull request may change without the transitions: skipping them is the gate's purpose.
# The native test entries are the ones AGENTS.md obliges every behavioural change to add, and
# both builds glob them, so neither build description has to learn about them.
ORDINARY_TRANSITION_PATHS = (
    "src/bs_analyzer.cpp",
    "tests/corpus/analyzer/errors/abstract_annotation_removed.barista",
    "docs/analyzer-discovery.md",
    "tests/native/analyzer_helpers.cpp",
    "tests/native/added_suite_test.cpp",
    "tests/native/native_test_runner.h",
    "tests/native/README.md",
)


def check_native_transition_scope(scope=None) -> str | None:
    """Require the transition decision to scope pull requests only, by its filter, failing open.

    The workflow pins the step that runs the decision, but not what the script decides. A filter
    missing a build input, an event other than a pull request being scoped, or an undecidable pull
    request being skipped would each leave the pinned steps byte-identical while silently dropping
    the verification, so each is exercised here against the script itself.
    """
    import io
    import tempfile
    from contextlib import redirect_stdout
    from unittest.mock import patch

    if scope is None:
        import native_transition_scope as scope
    commit = "0" * 40
    pull_request_head = "1" * 40
    base = "2" * 40

    def unreachable_git(*arguments):
        raise RuntimeError("git is unavailable")

    def merge_git(changes, parents=(commit, base, pull_request_head)):
        def git(*arguments):
            outputs = {"rev-parse": commit + "\n", "fetch": "", "rev-list": " ".join(parents) + "\n",
                       "diff": "".join(f"{status}\0{path}\0" for status, path in changes)}
            return outputs[arguments[0]]
        return git

    ordinary = [("M", path) for path in ORDINARY_TRANSITION_PATHS]
    try:
        for path in REQUIRED_TRANSITION_INPUTS:
            if scope.triggering_changes([("M", path)]) != [path]:
                return f"a pull request changing {path} must run the native build-system transitions"
        sources = [("A", "src/bs_added.cpp"), ("D", "src/bs_removed.cpp")]
        if scope.triggering_changes(sources) != [path for _, path in sources]:
            return "a pull request adding or removing an extension source must run the transitions"
        for event in ("push", "merge_group", "workflow_dispatch", "workflow_call", "schedule", ""):
            if scope.decide(event, pull_request_head, merge_git(ordinary))[0] is not True:
                return f"a {event or 'unnamed'} event must always run the native build-system transitions"
        undecidable = {
            "git is unavailable": (pull_request_head, unreachable_git),
            "the pull request head is unknown": ("", merge_git(ordinary)),
            "the checkout is not the pull request merge": (pull_request_head, merge_git(ordinary, (commit, base))),
        }
        for reason, (head, git) in undecidable.items():
            if scope.decide("pull_request", head, git)[0] is not True:
                return f"the transition decision must fail open when {reason}"
        if scope.decide("pull_request", pull_request_head, merge_git(ordinary))[0] is not False:
            return "a pull request changing only sources, corpus files or docs must skip the transitions"
        with tempfile.TemporaryDirectory() as directory, redirect_stdout(io.StringIO()):
            output = Path(directory) / "output"
            with patch.object(scope, "decide", side_effect=RuntimeError("decision failed")):
                status = scope.main(["--event", "pull_request", "--github-output", str(output)])
            if status != 0 or output.read_text() != "required=true\n":
                return "the transition decision must write required=true when it cannot decide"
    except Exception as error:
        return f"the transition decision could not be exercised: {error}"
    return None


def check_corpus_triage_wiring(workflow: str) -> str | None:
    """Require CI to execute every triage-supervised corpus against its residual-failure pin.

    Two committed guards already refuse a tampered pin, but both compare the pin against
    other committed bytes and neither evaluates a case. Only scripts/run_corpus_triage.py
    binds a pin to what the implementation actually does, and it reports through its exit
    status, so each pinned command must select the complete population -- an exact --case
    selection judges only the cases it names -- and must stay free to fail its job. One step
    per corpus is required, so adding a corpus without gating it is not expressible.

    The pinned command runs the whole corpus in four concurrent processes, each taking a
    contiguous slice. That is safe only because the supervisor refuses a run whose shards do
    not tile the population and whose own completion evidence does not account for every case
    the imported ledger declares, so a truncated or missing shard is an infrastructure error
    rather than a shorter baseline. The execution mode and the shard count are part of the
    exact comparison below for that reason: a smaller count is slower but sound, while any
    change to either alters what this job attests to.
    """
    try:
        job_steps = workflow_steps(workflow)
    except ValueError as error:
        return str(error)

    matches = [
        (job, step)
        for job, step in job_steps
        if isinstance(step.get("run"), str)
        and any("scripts/run_corpus_triage.py" in line for line in executable_lines(step["run"]))
    ]
    if len(matches) != len(CORPUS_TRIAGE_STEPS):
        return (
            f"CI must run corpus triage from exactly {len(CORPUS_TRIAGE_STEPS)} steps, one per "
            "triage-supervised corpus; scripts/run_corpus_triage.py is the only check that "
            "executes the cases the committed residual-failure pins describe"
        )
    for (job, step), required, corpus in zip(matches, CORPUS_TRIAGE_STEPS, TRIAGE_CORPORA):
        actual = {
            key: value.strip() if key == "run" and isinstance(value, str) else value
            for key, value in step.items()
            if key != "name"
        }
        if actual != required:
            return (
                f"the {corpus} corpus triage step must retain its validated inputs and its "
                "exact whole-population command"
            )
        # The step arms are redundant with the exact comparison above -- a `continue-on-error`
        # or `if` the pin does not carry already breaks equality. The operative arm is the job
        # one: disabling the whole build job leaves the step's own bytes untouched.
        if (
            condition_is_unreachable(job)
            or step_continues_on_error(step)
            or condition_is_unreachable(step)
        ):
            return (
                f"the {corpus} corpus triage must be able to fail its job: its exit status is "
                "the only check that every residual failure is still declared and owned"
            )
    return None


def check_static_checks_wiring(workflow: str) -> str | None:
    """Require a single, unconditional read-only static job consuming the shared tool pin."""
    try:
        import yaml
    except ImportError:
        return "CI audit requires PyYAML: python3 -m pip install -r tests/requirements.txt"
    try:
        document = yaml.load(workflow, Loader=yaml.BaseLoader)
        jobs = document.get("jobs", {})
        job = jobs.get("static-checks")
        if (not isinstance(job, dict) or set(job) - {"name", "runs-on", "permissions", "steps"}
                or job.get("name", "static-checks") != "static-checks"
                or job.get("runs-on") != "ubuntu-22.04"
                or job.get("permissions") != {"contents": "read"}):
            return "CI requires one unsuppressed Linux static-checks job outside the matrix"
        expected = [
            {"uses": "actions/checkout@v4", "with": {"persist-credentials": "false", "submodules": "false"}},
            {"uses": "actions/setup-python@v5", "with": {"python-version": "3.x"}},
            {"shell": "bash", "run": "python3 -m pip install -r scripts/requirements-format.txt -r tests/requirements.txt"},
            {"shell": "bash", "run": "python3 tests/test_check_format.py\npython3 tests/test_static_checks.py"},
            {"shell": "bash", "run": "python3 scripts/check_format.py"},
            {"shell": "bash", "run": "python3 scripts/add_license_header.py --check"},
        ]
        steps = job.get("steps")
        if not isinstance(steps, list) or len(steps) != len(expected):
            return "static-checks requires checkout, Python setup, pinned tools, regression tests, formatting and licenses"
        for index, (step, required) in enumerate(zip(steps, expected), 1):
            if not isinstance(step, dict):
                return f"static-checks step {index} must be a mapping"
            actual = {key: value.strip() if key == "run" and isinstance(value, str) else value
                      for key, value in step.items() if key != "name"}
            if actual != required:
                return f"static-checks step {index} must retain its validated inputs and unsuppressed check command"
        for name, other in jobs.items():
            if name != "static-checks" and isinstance(other, dict):
                if any(isinstance(step, dict) and "scripts/check_format.py" in step.get("run", "")
                       for step in other.get("steps", [])):
                    return "formatting must run only in the dedicated static-checks job"
    except (yaml.YAMLError, AttributeError, TypeError) as error:
        return f"invalid static-checks YAML: {error}"
    return None



def check_build_version_wiring(ci, package, action):
    """Pin actual matrix inputs, shared versions and the gate before the uploaded bytes."""
    try:
        config = load_config()
        expected_ci = {("linux", "x86_64", "template_debug"), ("windows", "x86_64", "template_debug"),
                       ("windows", "x86_64", "template_release"), ("macos", "universal", "template_debug"),
                       ("ios", "arm64", "template_debug"), ("android", "arm64", "template_debug"),
                       ("web", "wasm32", "template_release")}
        expected_package = {(platform, arch) for platform, arches in {
            "linux": ("x86_64", "x86_32", "arm64", "arm32"), "windows": ("x86_64", "x86_32", "arm64"),
            "macos": ("universal",), "android": ("x86_64", "x86_32", "arm64", "arm32"),
            "ios": ("arm64",), "web": ("wasm32",)}.items() for arch in arches}
        def active(step):
            return ("if" not in step and step.get("continue-on-error", "false") in (False, "false")
                    and not SUPPRESSED_STATUS.search(step.get("run", "")))
        normalize = lambda value: " ".join(value.split())
        resolver = 'python3 scripts/build_config.py --platform ${{ matrix.target.platform }} --format github >> "$GITHUB_OUTPUT"'
        gate = ('python3 scripts/verify_build_artifacts.py --binary-dir bin --platform ${{ matrix.target.platform }} '
                '--architecture ${{ matrix.target.arch }} --target ${{ matrix.target-type }} '
                '--api ${{ steps.versions.outputs.godot_api }} --precision ${{ matrix.float-precision }}')
        build_command = ('scons api_version=${{ steps.versions.outputs.godot_api }} target=${{ matrix.target-type }} '
                         'platform=${{ matrix.target.platform }} arch=${{ matrix.target.arch }} precision=${{ matrix.float-precision }}')
        for name, document in (("CI", ci), ("packaging", package)):
            job = document["jobs"]["build"]
            if not active(job):
                raise ValueError(name + " build job must be unconditional and unsuppressed")
            matrix = job["strategy"]["matrix"]
            if name == "CI":
                rows = matrix["include"]
                actual = {(row["target"]["platform"], row["target"]["arch"], row["target-type"]) for row in rows}
                if actual != expected_ci or len(rows) != len(expected_ci) or any(row["float-precision"] != config["precision"] for row in rows):
                    raise ValueError("CI platform/architecture/target/precision matrix drift")
            else:
                rows = matrix["target"]
                actual = {(row["platform"], row["arch"]) for row in rows}
                if actual != expected_package or len(rows) != len(expected_package) or matrix["target-type"] != ["template_debug", "template_release"] or matrix["float-precision"] != [config["precision"]]:
                    raise ValueError("manual packaging platform/architecture/target/precision matrix drift")
            steps = job["steps"]
            versions = [index for index, step in enumerate(steps) if step.get("id") == "versions" and normalize(step.get("run", "")) == resolver and active(step)]
            builds = [index for index, step in enumerate(steps) if normalize(step.get("run", "")) == build_command and active(step)]
            gates = [index for index, step in enumerate(steps) if normalize(step.get("run", "")) == gate and active(step)]
            setups = [index for index, step in enumerate(steps) if step.get("uses") == "./.github/actions/setup-godot-cpp"]
            if any(len(items) != 1 for items in (versions, builds, gates, setups)) or not versions[0] < setups[0] < builds[0] < gates[0]:
                raise ValueError(name + " requires shared resolution, setup, build and actual bin artifact gate in order")
            setup = steps[setups[0]]
            for field, expected in (("platform", "${{ matrix.target.platform }}"), ("em-version", "${{ steps.versions.outputs.emscripten }}"), ("windows-compiler", "${{ steps.versions.outputs.windows_compiler }}")):
                require_equal(name + " setup " + field, expected, setup["with"][field])
            for step in steps:
                if "GODOT_VERSION" in step.get("env", {}):
                    require_equal("runtime version source", "${{ steps.versions.outputs.godot_runtime }}", step["env"]["GODOT_VERSION"])
            if name == "packaging":
                uploads = [index for index, step in enumerate(steps) if step.get("uses", "").startswith("actions/upload-artifact@")]
                if len(uploads) != 1 or not gates[0] < uploads[0]:
                    raise ValueError("actual artifact identity must gate upload")
                require_equal("packaging upload tree", "${{ github.workspace }}/bin/**", steps[uploads[0]]["with"]["path"].strip())
        for field in ("em-version", "mingw-version", "ndk-version", "scons-version", "windows-compiler"):
            require_equal("setup default " + field, "", action["inputs"][field]["default"])
        steps = action["runs"]["steps"]
        resolver_steps = [step for step in steps if step.get("id") == "versions" and active(step)]
        if len(resolver_steps) != 1 or not normalize(resolver_steps[0]["run"]).startswith("python3 scripts/build_config.py --setup --platform"):
            raise ValueError("setup action must resolve shared defaults and explicit overrides")
        for step in steps:
            if step.get("id") != "versions":
                for field in ("em-version", "mingw-version", "ndk-version", "scons-version", "windows-compiler"):
                    if "inputs." + field in str(step):
                        raise ValueError("setup action bypasses resolved tool versions")
    except (KeyError, TypeError, ValueError) as error:
        return str(error)
    return None


def main() -> int:
    config = load_config()
    selected_api_path = api_path(config)
    validate_api_file(config, selected_api_path)
    workflow_path = ROOT / ".github" / "workflows" / "ci.yml"

    api_precision = json.loads(selected_api_path.read_text())["header"]["precision"]
    workflow = workflow_path.read_text()
    import yaml
    complaint = check_build_version_wiring(yaml.load(workflow, Loader=yaml.BaseLoader),
        yaml.load((ROOT / ".github/workflows/make_build.yml").read_text(), Loader=yaml.BaseLoader),
        yaml.load((ROOT / ".github/actions/setup-godot-cpp/action.yml").read_text(), Loader=yaml.BaseLoader))
    if complaint:
        print(complaint)
        return 1
    descriptor = (ROOT / "project/bin/barista_script.gdextension").read_text()
    match = re.search(r'compatibility_minimum\s*=\s*"([^"]+)"', descriptor)
    if not match or match[1] != config["godot_api"]:
        print("ordinary descriptor compatibility differs from shared Godot API")
        return 1
    matrix_precisions = re.findall(r"^\s+float-precision:\s+(\w+)\s*$", workflow, re.MULTILINE)

    if not matrix_precisions:
        print("No CI float-precision matrix entries found")
        return 1

    incompatible = sorted({precision for precision in matrix_precisions if precision != api_precision})
    if incompatible:
        print(
            f"CI requests {', '.join(incompatible)} precision, but the Godot {config['godot_api']} API is {api_precision} precision"
        )
        return 1

    if "  push:\n    branches: [main]\n" not in workflow:
        print("CI push events must be limited to main to avoid duplicating pull request runs")
        return 1

    corpus_wiring_complaint = check_corpus_reproducibility_wiring(workflow)
    if corpus_wiring_complaint is not None:
        print(corpus_wiring_complaint)
        return 1

    static_complaint = check_static_checks_wiring(workflow)
    if static_complaint is not None:
        print(static_complaint)
        return 1

    suite_wiring_complaint = check_gdscript_suite_wiring(workflow)
    if suite_wiring_complaint is not None:
        print(suite_wiring_complaint)
        return 1

    native_complaint = check_native_suite_wiring(workflow)
    if native_complaint is not None:
        print(native_complaint)
        return 1

    linux_verification_complaint = check_linux_verification_wiring(workflow)
    if linux_verification_complaint is not None:
        print(linux_verification_complaint)
        return 1

    transition_scope_complaint = check_native_transition_scope()
    if transition_scope_complaint is not None:
        print(transition_scope_complaint)
        return 1

    triage_complaint = check_corpus_triage_wiring(workflow)
    if triage_complaint is not None:
        print(triage_complaint)
        return 1

    baseline_complaint = check_corpus_baseline()
    if baseline_complaint is not None:
        print(baseline_complaint)
        return 1

    print(f"CI configuration matches the Godot {config['godot_api']} API ({api_precision}) and avoids duplicate PR runs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
