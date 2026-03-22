from pathlib import Path

from flask import Flask

from .api.routes import api_bp
from .db.database import initialize_database
from .services.inventory_service import InventoryService


def create_app(test_config=None):
    app = Flask(__name__, template_folder="templates")

    project_root = Path(__file__).resolve().parents[2]
    app.config.update(
        PROJECT_ROOT=str(project_root),
        EVENT_OUTPUT_DIR=str(project_root / "data" / "sessions"),
        DATABASE_PATH=str(project_root / "data" / "runtime" / "fridge_demo.sqlite3"),
        JSON_SORT_KEYS=False,
        TESTING=False,
    )

    if test_config:
        app.config.update(test_config)

    Path(app.config["EVENT_OUTPUT_DIR"]).mkdir(parents=True, exist_ok=True)
    Path(app.config["DATABASE_PATH"]).parent.mkdir(parents=True, exist_ok=True)
    initialize_database(app.config["DATABASE_PATH"])

    app.config["inventory_service"] = InventoryService(
        db_path=app.config["DATABASE_PATH"],
        event_dir=app.config["EVENT_OUTPUT_DIR"],
    )

    app.register_blueprint(api_bp)
    return app


app = create_app()


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)
