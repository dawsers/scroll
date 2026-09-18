"""Cross-slice regression test for the full space-templates stack (issue #6).

Unlike tests/test_space_template.py (command/IPC) and
tests/test_lua_space_template.py (Lua binding), which each exercise one
surface in isolation, this test deliberately crosses all of them in a
single flow -- save via command, read via IPC, apply via Lua, re-read via
IPC again -- plus a legacy `space`/`get_spaces` regression check run in the
same session, to catch interaction bugs a single-surface test would miss
(e.g. a cache invalidation issue only visible when save and get/apply hit
the template from different code paths in the same process).

NOTE: like the other space-template test files, this was authored without
a working meson/ninja/wlroots build and has not been run; see the
space-templates PR chain. Only py_compile/pyflakes-level verification was
possible in that sandbox.
"""

import json
from typing import Any, Dict, List

from test_utils import ScrollInstance, wait_for_title_contains_map, wayland_client


def _collect_slot_titles(template: Dict[str, Any]) -> Dict[str, str]:
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


def test_space_template_cross_slice_save_command_get_ipc_apply_lua(
    scroll_compositor: ScrollInstance,
) -> None:
    inst = scroll_compositor

    with wayland_client(inst, "CrossSlice1"):
        wait_for_title_contains_map(inst, "CrossSlice1")
        with wayland_client(inst, "CrossSlice2"):
            wait_for_title_contains_map(inst, "CrossSlice2")

            # 1. Save via the `space_template` command (G4).
            res = inst.cmd("space_template save cross_slice --with-hints")
            assert res and res[0]["success"], f"save failed: {res}"

            # 2. Read it back via the get_space_template IPC message (G4),
            # not the command path, to confirm both surfaces agree.
            template = inst.get_space_template("cross_slice")
            assert template.get("name") == "cross_slice"
            slot_titles = _collect_slot_titles(template)
            assert len(slot_titles) == 2

            mapping_entries: List[str] = []
            for slot, title in slot_titles.items():
                node = wait_for_title_contains_map(inst, title)
                mapping_entries.append(f'{{ slot = "{slot}", con_id = {node["id"]} }}')
            lua_mappings = "{ " + ", ".join(mapping_entries) + " }"

            # 3. Apply via the Lua binding (G5), not the command/IPC apply
            # path, to confirm the shared glue behaves identically there.
            result = inst.execute_lua(
                f'return scroll.space_template_apply("cross_slice", {lua_mappings})'
            )
            assert result is True, f"expected `true`, got {result!r}"

            # 4. get_space_template again post-apply: the template itself
            # must be unchanged (apply reads it, never mutates the saved
            # file -- G3's "dissolve" contract).
            template_after = inst.get_space_template("cross_slice")
            assert template_after == template

            # 5. Legacy `space`/`get_spaces` must still work, untouched, in
            # the same session.
            res = inst.cmd("space save cross_slice_legacy")
            assert res and res[0]["success"], f"legacy space save failed: {res}"
            ipc = inst.ipc
            ipc._send(122, "")  # IPC_GET_SPACES
            reply_type, payload = ipc._recv()
            assert reply_type == 122
            spaces = json.loads(payload)
            assert any(s.get("name") == "cross_slice_legacy" for s in spaces)
            res = inst.cmd("space delete cross_slice_legacy")
            assert res and res[0]["success"]
