CANONICAL_CATEGORIES = {
    "produce",
    "fresh_protein",
    "beverage_dairy",
    "packaged_food",
    "other",
}


def normalize_category(category):
    if category is None:
        return "other"

    value = str(category).strip()
    if not value:
        return "other"

    normalized = value.lower()

    produce_aliases = {
        "produce",
        "fruit",
        "vegetable",
        "蔬果类",
        "水果",
        "蔬菜",
    }
    fresh_protein_aliases = {
        "fresh_protein",
        "protein",
        "meat",
        "egg",
        "seafood",
        "肉蛋生鲜类",
        "蛋白类",
        "肉类",
        "蛋类",
        "海鲜",
    }
    beverage_dairy_aliases = {
        "beverage_dairy",
        "drink",
        "beverage",
        "dairy",
        "milk",
        "饮料乳品类",
        "饮品",
        "乳品",
        "牛奶",
    }
    packaged_food_aliases = {
        "packaged_food",
        "packaged",
        "packaged-food",
        "package",
        "snack",
        "包装食品类",
        "包装食品",
        "零食",
    }
    other_aliases = {
        "other",
        "unknown",
        "misc",
        "其他",
        "未分类",
    }

    if normalized in produce_aliases or value in produce_aliases:
        return "produce"

    if normalized in fresh_protein_aliases or value in fresh_protein_aliases:
        return "fresh_protein"

    if normalized in beverage_dairy_aliases or value in beverage_dairy_aliases:
        return "beverage_dairy"

    if normalized in packaged_food_aliases or value in packaged_food_aliases:
        return "packaged_food"

    if normalized in other_aliases or value in other_aliases:
        return "other"

    return "other"
