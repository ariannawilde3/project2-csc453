from pathlib import Path

def compare_exact(actual_trace, actual_stats, expected_trace, expected_stats):
    atrace = Path(actual_trace).read_text().strip().splitlines()
    astats = Path(actual_stats).read_text().strip().splitlines()
    etrace = Path(expected_trace).read_text().strip().splitlines()
    estats = Path(expected_stats).read_text().strip().splitlines()
    if atrace != etrace:
        return False, "trace mismatch\nEXPECTED:\n" + "\n".join(etrace) + "\nACTUAL:\n" + "\n".join(atrace)
    if astats != estats:
        return False, "stats mismatch\nEXPECTED:\n" + "\n".join(estats) + "\nACTUAL:\n" + "\n".join(astats)
    return True, "ok"
