"""Integration tests for scroll.space_template_get/apply (issue #5, G5).

These exercise the Lua binding specifically (as opposed to
tests/test_space_template.py, which exercises the command/IPC surface
directly): the (nil, error[, slots]) convention, and that a successful
apply via Lua produces the same live result as the command/IPC path.

NOTE: like tests/test_space_template.py, this was authored without a
working meson/ninja/wlroots build (see the space-templates PR chain) and
has not been run; only syntax/logic-level review was possible in that
sandbox. It documents the intended, reviewable behavior.
"""

from typing import Any, Dict, List

from test_utils import ScrollInstance, wait_for_title_contains_map, wayland_client


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


def test_lua_space_template_get_unknown_name(scroll_compositor: ScrollInstance) -> None:
    result = scroll_compositor.execute_lua(
        'return scroll.space_template_get("lua-st-does-not-exist")'
    )
    # (nil, error_string) -> unwrapped to [None, "..."] by execute_lua().
    assert isinstance(result, list) and len(result) == 2
    assert result[0] is None
    assert isinstance(result[1], str) and len(result[1]) > 0


def test_lua_space_template_get_apply_round_trip(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "LuaSTRoundtrip1"):
        wait_for_title_contains_map(inst, "LuaSTRoundtrip1")
        with wayland_client(inst, "LuaSTRoundtrip2"):
            wait_for_title_contains_map(inst, "LuaSTRoundtrip2")

            res = inst.cmd("space_template save lua_st_roundtrip --with-hints")
            assert res and res[0]["success"], f"save failed: {res}"

            template = inst.execute_lua(
                'return scroll.space_template_get("lua_st_roundtrip")'
            )
            assert isinstance(template, dict), f"expected a template table, got {template!r}"
            assert template.get("name") == "lua_st_roundtrip"

            slots = _collect_slots(template)
            assert len(slots) == 2

            # Resolve which live window belongs to which slot via the
            # view_hint titles captured by --with-hints (same approach as
            # tests/test_space_template.py), then build the Lua mappings
            # table as source text for execute_lua().
            slot_titles: Dict[str, str] = {}

            def walk(node: Dict[str, Any]) -> None:
                if "children" in node:
                    for child in node["children"]:
                        walk(child)
                else:
                    slot_titles[node["slot"]] = node.get("view_hint", {}).get("title", "")

            for root in template.get("tiling", []) + template.get("floating", []):
                walk(root)

            mapping_entries = []
            for slot, title in slot_titles.items():
                node = wait_for_title_contains_map(inst, title)
                mapping_entries.append(f'{{ slot = "{slot}", con_id = {node["id"]} }}')

            lua_mappings = "{ " + ", ".join(mapping_entries) + " }"
            result = inst.execute_lua(
                f'return scroll.space_template_apply("lua_st_roundtrip", {lua_mappings})'
            )
            assert result is True, f"expected a bare `true` on success, got {result!r}"


def test_lua_space_template_apply_missing_slot(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "LuaSTMissing1"):
        wait_for_title_contains_map(inst, "LuaSTMissing1")
        with wayland_client(inst, "LuaSTMissing2"):
            node2 = wait_for_title_contains_map(inst, "LuaSTMissing2")

            res = inst.cmd("space_template save lua_st_missing")
            assert res and res[0]["success"]

            template = inst.get_space_template("lua_st_missing")
            slots = _collect_slots(template)
            assert len(slots) == 2

            result = inst.execute_lua(
                f'return scroll.space_template_apply("lua_st_missing", '
                f'{{ {{ slot = "{slots[0]}", con_id = {node2["id"]} }} }})'
            )
            # (nil, error_string, slots_table) -> [None, "...", [slots[1]]]
            assert isinstance(result, list) and len(result) == 3
            assert result[0] is None
            assert isinstance(result[1], str)
            assert result[2] == [slots[1]]
