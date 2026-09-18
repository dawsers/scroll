"""Integration tests for the space_template command + IPC surface (issue #4).

These exercise the full save -> get_space_template -> apply_space_template
path against a real (headless) compositor, the same way test_space_crash.py
etc. exercise the legacy `space` command. Each test gets its own isolated
$HOME (see tests/conftest.py's run_compositor()), so saved templates never
leak between tests or touch a real user's config.

NOTE: this file was authored without a working meson/ninja/wlroots build in
the sandbox used to write it (see the space-templates PR chain for details),
so it has not been run. It documents the intended, reviewable behavior and
should run correctly once built -- see the Linux container planned for real
compilation/test verification.
"""

import json
from typing import Any, Dict, List

from test_utils import ScrollInstance, wait_for_title_contains_map, wayland_client


def _collect_slot_titles(template: Dict[str, Any]) -> Dict[str, str]:
    """Walks a get_space_template() reply's tiling+floating trees, returning
    {slot_name: view_hint.title} for every leaf. Requires the template to
    have been saved with --with-hints."""
    slots: Dict[str, str] = {}

    def walk(node: Dict[str, Any]) -> None:
        if "children" in node:
            for child in node["children"]:
                walk(child)
        else:
            slots[node["slot"]] = node.get("view_hint", {}).get("title", "")

    for root in template.get("tiling", []) + template.get("floating", []):
        walk(root)
    return slots


def _collect_slots(template: Dict[str, Any]) -> List[str]:
    slots: List[str] = []

    def walk(node: Dict[str, Any]) -> None:
        if "children" in node:
            for child in node["children"]:
                walk(child)
        else:
            slots.append(node["slot"])

    for root in template.get("tiling", []) + template.get("floating", []):
        walk(root)
    return slots


def test_space_template_save_get_apply_round_trip(
    scroll_compositor: ScrollInstance,
) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "STRoundtrip1"):
        wait_for_title_contains_map(inst, "STRoundtrip1")
        with wayland_client(inst, "STRoundtrip2"):
            wait_for_title_contains_map(inst, "STRoundtrip2")

            res = inst.cmd("space_template save st_roundtrip --with-hints")
            assert res and res[0]["success"], f"save failed: {res}"

            template = inst.get_space_template("st_roundtrip")
            assert template.get("version") == 1
            assert template.get("name") == "st_roundtrip"
            assert "success" not in template  # a real template, not an error object

            slot_titles = _collect_slot_titles(template)
            assert len(slot_titles) == 2

            mappings = []
            for slot, title in slot_titles.items():
                node = wait_for_title_contains_map(inst, title)
                mappings.append({"slot": slot, "con_id": node["id"]})

            apply_result = inst.apply_space_template("st_roundtrip", mappings)
            assert apply_result.get("success") is True, apply_result


def test_space_template_apply_missing_slot_reports_and_does_not_mutate(
    scroll_compositor: ScrollInstance,
) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "STMissing1"):
        wait_for_title_contains_map(inst, "STMissing1")
        with wayland_client(inst, "STMissing2"):
            node2 = wait_for_title_contains_map(inst, "STMissing2")

            res = inst.cmd("space_template save st_missing")
            assert res and res[0]["success"], f"save failed: {res}"

            template = inst.get_space_template("st_missing")
            slots = _collect_slots(template)
            assert len(slots) == 2

            tree_before = inst.get_tree()

            # Bind only one of the two slots -- apply must fail, name the
            # unbound slot, and mutate nothing.
            apply_result = inst.apply_space_template(
                "st_missing", [{"slot": slots[0], "con_id": node2["id"]}]
            )
            assert apply_result.get("success") is False
            assert slots[1] in apply_result.get("slots", [])

            tree_after = inst.get_tree()
            assert tree_before == tree_after, "apply mutated the tree despite failing validation"


def test_space_template_apply_ambiguous_view_rejected(
    scroll_compositor: ScrollInstance,
) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "STAmbiguous1"):
        node1 = wait_for_title_contains_map(inst, "STAmbiguous1")
        with wayland_client(inst, "STAmbiguous2"):
            wait_for_title_contains_map(inst, "STAmbiguous2")

            res = inst.cmd("space_template save st_ambiguous")
            assert res and res[0]["success"]

            template = inst.get_space_template("st_ambiguous")
            slots = _collect_slots(template)
            assert len(slots) == 2

            # Bind the same live view to both slots -- ambiguous, must fail.
            apply_result = inst.apply_space_template(
                "st_ambiguous",
                [
                    {"slot": slots[0], "con_id": node1["id"]},
                    {"slot": slots[1], "con_id": node1["id"]},
                ],
            )
            assert apply_result.get("success") is False
            assert set(slots) <= set(apply_result.get("slots", []))


def test_space_template_get_unknown_name(scroll_compositor: ScrollInstance) -> None:
    result = scroll_compositor.get_space_template("st-definitely-does-not-exist")
    assert result.get("success") is False
    assert "error" in result


def test_space_template_apply_malformed_request_rejected(
    scroll_compositor: ScrollInstance,
) -> None:
    ipc = scroll_compositor.ipc
    ipc._send(126, "not json")  # IPC_APPLY_SPACE_TEMPLATE
    reply_type, payload = ipc._recv()
    assert reply_type == 126
    result = json.loads(payload)
    assert result.get("success") is False

    # Missing "mappings" entirely.
    ipc._send(126, json.dumps({"name": "whatever"}))
    reply_type, payload = ipc._recv()
    result = json.loads(payload)
    assert result.get("success") is False

    # "workspace" naming something other than "current" is rejected in v1.
    ipc._send(126, json.dumps({"name": "whatever", "workspace": "2", "mappings": []}))
    reply_type, payload = ipc._recv()
    result = json.loads(payload)
    assert result.get("success") is False


def test_legacy_space_and_get_spaces_unaffected(scroll_compositor: ScrollInstance) -> None:
    """Regression guard: the new space_template surface must not change the
    legacy `space` command or `get_spaces` IPC message."""
    inst = scroll_compositor
    with wayland_client(inst, "STLegacy"):
        wait_for_title_contains_map(inst, "STLegacy")

        res = inst.cmd("space save st_legacy_check")
        assert res and res[0]["success"], f"legacy space save failed: {res}"

        ipc = inst.ipc
        ipc._send(122, "")  # IPC_GET_SPACES
        reply_type, payload = ipc._recv()
        assert reply_type == 122
        spaces_list = json.loads(payload)
        assert any(s.get("name") == "st_legacy_check" for s in spaces_list)

        res = inst.cmd("space delete st_legacy_check")
        assert res and res[0]["success"]
