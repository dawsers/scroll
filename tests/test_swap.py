from conftest import ScrollInstance


def test_swap_invalid_con_id(scroll_compositor: ScrollInstance) -> None:
    # Try swap with invalid con_id format
    res: list = scroll_compositor.cmd("swap container with con_id invalid123")
    assert not res[0]["success"]
    assert "Invalid container ID" in res[0].get("error", "")

    # Try swap with valid con_id format but non-existent
    res = scroll_compositor.cmd("swap container with con_id 999999")
    assert not res[0]["success"]
    assert "Failed to find con_id '999999'" in res[0].get("error", "")
