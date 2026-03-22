from flask import Blueprint, current_app, jsonify, redirect, render_template, request, url_for

api_bp = Blueprint("api", __name__)


def get_service():
    return current_app.config["inventory_service"]


@api_bp.get("/")
def index():
    service = get_service()
    return render_template(
        "index.html",
        inventory=service.list_inventory(),
        events=service.list_events(),
        pending_confirmations=service.list_pending_confirmations(),
    )


@api_bp.get("/health")
def health():
    return jsonify(get_service().health())


@api_bp.get("/events")
def events():
    service = get_service()
    return jsonify({"events": service.list_events()})


@api_bp.get("/inventory")
def inventory():
    service = get_service()
    return jsonify(
        {
            "inventory": service.list_inventory(),
            "pending_confirmations": service.list_pending_confirmations(),
        }
    )


@api_bp.post("/confirm")
def confirm():
    payload = request.get_json(silent=True) or request.form.to_dict()
    result = get_service().confirm(payload)

    if request.is_json:
        return jsonify(result)

    return redirect(url_for("api.index"))
