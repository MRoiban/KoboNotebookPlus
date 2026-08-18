#!/usr/bin/env python3

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def verify_observer_is_passive(source_dir: Path) -> None:
    source = (source_dir / "passive_input_router.cc").read_text(
        encoding="utf-8"
    )
    event_filter = source.split(
        "bool eventFilter(QObject* watched, QEvent* event) override {", 1
    )
    if len(event_filter) != 2:
        event_filter = source.split(
            "bool PassiveInputRouter::eventFilter("
            "QObject* watched, QEvent* event) {",
            1,
        )
    if len(event_filter) != 2:
        raise SystemExit("passive input router eventFilter was not found")
    event_filter = event_filter[1].split(
        "\nvoid PassiveInputRouter::timerEvent", 1
    )[0]
    forbidden = (
        "accept(",
        "setAccepted(",
        "return true;",
        "sendEvent(",
        "postEvent(",
        "notePadUndo(",
        "notePadRedo(",
        "QObject::eventFilter(",
    )
    violations = [token for token in forbidden if token in event_filter]
    if violations:
        raise SystemExit(
            "passive input router may consume or re-enter touch dispatch: "
            + ", ".join(violations)
        )
    if event_filter.count("return false;") != 1:
        raise SystemExit(
            "passive input router must have exactly one false return"
        )

    router_header = (source_dir / "passive_input_router.h").read_text(
        encoding="utf-8"
    )
    if "bool eventFilter(QObject* watched, QEvent* event) final;" not in (
        router_header
    ):
        raise SystemExit("passive event filter is not final")

    core_header = (source_dir / "passive_input.h").read_text(encoding="utf-8")
    if "#include <Q" in core_header:
        raise SystemExit("passive recognizer core unexpectedly depends on Qt")

    swipe_header = (source_dir / "three_finger_swipe.h").read_text(
        encoding="utf-8"
    )
    if "public passive_input::GestureRecognizer" not in swipe_header:
        raise SystemExit(
            "three-finger swipe is not a registered passive recognizer"
        )

    integration = (source_dir / "notebook_gestures.cc").read_text(
        encoding="utf-8"
    )
    registrations = (
        "gestures().registerRecognizer(horizontalThreeFingerSwipe_)",
    )
    missing = [item for item in registrations if item not in integration]
    if missing:
        raise SystemExit(
            "passive recognizers are not registered with the router: "
            + ", ".join(missing)
        )
    forbidden_layers_bridge = (
        "downThreeFingerSwipe_",
        "OpenLayersNotebookGestureAction",
        "openNotebookOverflowMenu",
        "ThreeFingerSwipeAxis::Vertical",
    )
    retained = [
        item for item in forbidden_layers_bridge if item in integration
    ]
    if retained:
        raise SystemExit(
            "disabled three-finger-down Layers gesture remains: "
            + ", ".join(retained)
        )
    forbidden_chord = (
        "HeldInputChord",
        "InputChord",
        "EnterLassoChordAction",
        "RestoreDefaultPenChordAction",
        "stylusTaskButtonHeld",
        "recentFingerContacts",
    )
    retained_chord = [
        item for item in forbidden_chord
        if item in integration or item in router_header or item in core_header
    ]
    if retained_chord:
        raise SystemExit(
            "disabled held-input chord remains: "
            + ", ".join(retained_chord)
        )
    router = (source_dir / "passive_input_router.cc").read_text(
        encoding="utf-8"
    )
    if "tabletEvent" in router or "InputStateFrame" in router:
        raise SystemExit(
            "disabled held-input event path remains in passive router"
        )


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    compiler_name = os.environ.get("HOST_CXX", "c++")
    compiler = shutil.which(compiler_name)
    if compiler is None:
        raise SystemExit(f"host C++ compiler not found: {compiler_name}")

    source_dir = root / "mods" / "custom-notebook-templates" / "src"
    verify_observer_is_passive(source_dir)
    core = source_dir / "passive_input.cc"
    recognizer = source_dir / "three_finger_swipe.cc"
    swipe_test = root / "scripts" / "test-three-finger-swipe.cc"
    registry_test = root / "scripts" / "test-passive-input.cc"
    with tempfile.TemporaryDirectory(prefix="kobo-gesture-test-") as temporary:
        common = [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{source_dir}",
        ]
        swipe_executable = Path(temporary) / "three-finger-swipe-test"
        subprocess.run(
            common
            + [
                str(core),
                str(recognizer),
                str(swipe_test),
                "-o",
                str(swipe_executable),
            ],
            check=True,
        )
        subprocess.run([str(swipe_executable)], check=True)

        registry_executable = Path(temporary) / "passive-input-test"
        subprocess.run(
            common
            + [
                str(core),
                str(registry_test),
                "-o",
                str(registry_executable),
            ],
            check=True,
        )
        subprocess.run([str(registry_executable)], check=True)


if __name__ == "__main__":
    main()
