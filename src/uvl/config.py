import json

from .paths import CONFIG_PATH, UVL_HOME


DEFAULT_ENTRIES = {
    "bun": "node_modules",
    "npm": "node_modules",
    "pnpm": "node_modules",
    "yarn": "node_modules",
    "uv": ".venv",
    "pip": ".venv",
    "poetry": ".venv",
}


def load_config():
    if not CONFIG_PATH.exists():
        return {"registrations": {}}
    try:
        data = json.loads(CONFIG_PATH.read_text())
    except json.JSONDecodeError:
        return {"registrations": {}}
    if not isinstance(data, dict):
        return {"registrations": {}}
    data.setdefault("registrations", {})
    return data


def save_config(config):
    UVL_HOME.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(json.dumps(config, indent=2, sort_keys=True) + "\n")


def get_registration(tool):
    return load_config()["registrations"].get(tool)


def register_tool_config(tool, entry):
    config = load_config()
    config["registrations"][tool] = {"entry": entry}
    save_config(config)


def unregister_tool_config(tool):
    config = load_config()
    if tool in config["registrations"]:
        del config["registrations"][tool]
        save_config(config)
        return True
    return False
