import { manualAdjustInventory } from "../../api/confirm"

interface CategoryOption {
  value: string
  label: string
  className: string
}

interface InventoryFormData {
  mode: "add" | "edit"
  pageTitle: string
  name: string
  category: string
  categoryOptions: CategoryOption[]
  countValue: number
  remainLevel: number
  sliderValue: number
  remainPercentText: string
  saving: boolean
  deleting: boolean
  isEditMode: boolean
  originalName: string
  originalCategory: string
}

const CATEGORY_OPTIONS = [
  { value: "fruit_vegetable", label: "果蔬类" },
  { value: "meat_egg_fresh", label: "肉蛋生鲜类" },
  { value: "drink", label: "饮料类" },
  { value: "packaged_food", label: "包装食品类" },
  { value: "other", label: "其他" },
]

function getRemainPercentText(remainLevel: number) {
  return `${Math.round(remainLevel * 100)}%`
}

function buildCategoryOptions(currentCategory: string) {
  return CATEGORY_OPTIONS.map((option) => ({
    ...option,
    className:
      option.value === currentCategory
        ? "category-chip category-chip-active"
        : "category-chip",
  }))
}

function normalizeCategory(category: string) {
  if (category === "fruit" || category === "vegetable" || category === "produce") {
    return "fruit_vegetable"
  }

  if (category === "protein" || category === "fresh_protein" || category === "meat_egg_fresh") {
    return "meat_egg_fresh"
  }

  if (category === "drink" || category === "beverage_dairy") {
    return "drink"
  }

  if (category === "packaged_food") {
    return "packaged_food"
  }

  if (category === "unknown" || category === "other" || !category) {
    return "other"
  }

  return "other"
}

Page<InventoryFormData, WechatMiniprogram.IAnyObject>({
  data: {
    mode: "add",
    pageTitle: "新增库存",
    name: "",
    category: "other",
    categoryOptions: buildCategoryOptions("other"),
    countValue: 1,
    remainLevel: 0.9,
    sliderValue: 90,
    remainPercentText: "90%",
    saving: false,
    deleting: false,
    isEditMode: false,
    originalName: "",
    originalCategory: "",
  },

  onLoad(options: Record<string, string>) {
    const mode = options.mode === "edit" ? "edit" : "add"

    if (mode === "add") {
      wx.setNavigationBarTitle({
        title: "新增库存",
      })
      return
    }

    const currentItem = wx.getStorageSync("inventory_form_item") as {
      id: number
      name: string
      category: string
      count: number
      remain_level: number
    } | null

    if (!currentItem) {
      wx.showToast({
        title: "未找到要编辑的库存",
        icon: "none",
      })
      wx.navigateBack()
      return
    }

    const selectedCategory = normalizeCategory(currentItem.category)

    this.setData({
      mode: "edit",
      pageTitle: "编辑库存",
      name: currentItem.name === "unknown" ? "" : currentItem.name,
      category: selectedCategory,
      categoryOptions: buildCategoryOptions(selectedCategory),
      countValue: currentItem.count,
      remainLevel: currentItem.remain_level,
      sliderValue: Math.round(currentItem.remain_level * 100),
      remainPercentText: getRemainPercentText(currentItem.remain_level),
      isEditMode: true,
      originalName: currentItem.name,
      originalCategory: currentItem.category,
    })

    wx.setNavigationBarTitle({
      title: "编辑库存",
    })
  },

  handleNameInput(event: WechatMiniprogram.CustomEvent<{ value: string }>) {
    this.setData({
      name: event.detail.value,
    })
  },

  handleCategorySelect(event: WechatMiniprogram.BaseEvent) {
    const { value } = event.currentTarget.dataset as { value: string }
    const nextCategory = value || "unknown"

    this.setData({
      category: nextCategory,
      categoryOptions: buildCategoryOptions(nextCategory),
    })
  },

  handleDecreaseCount() {
    this.setData({
      countValue: Math.max(0, this.data.countValue - 1),
    })
  },

  handleIncreaseCount() {
    this.setData({
      countValue: this.data.countValue + 1,
    })
  },

  handleRemainChange(event: WechatMiniprogram.CustomEvent<{ value: number }>) {
    const remainLevel = Number(event.detail.value) / 100
    this.setData({
      remainLevel,
      sliderValue: Number(event.detail.value),
      remainPercentText: getRemainPercentText(remainLevel),
    })
  },

  async handleSubmit() {
    const name = this.data.name.trim()
    const category = this.data.category || "unknown"
    const count = this.data.countValue
    const remainLevel = this.data.remainLevel

    if (!name) {
      wx.showToast({
        title: "请输入名称",
        icon: "none",
      })
      return
    }

    if (this.data.mode === "add" && count <= 0) {
      wx.showToast({
        title: "新增库存数量至少为 1",
        icon: "none",
      })
      return
    }

    this.setData({
      saving: true,
    })

    try {
      await manualAdjustInventory({
        item_name: name,
        category,
        count,
        remain_level: remainLevel,
        note: this.data.mode === "add" ? "mini-program manual add" : "mini-program manual edit",
      })

      wx.showToast({
        title: "保存成功",
        icon: "success",
      })

      setTimeout(() => {
        wx.navigateBack()
      }, 250)
    } catch (error) {
      wx.showToast({
        title: error instanceof Error ? error.message : "保存失败",
        icon: "none",
      })
    } finally {
      this.setData({
        saving: false,
      })
    }
  },

  handleDelete() {
    wx.showModal({
      title: "确认删除",
      content: "删除会把这条库存数量调整为 0，是否继续？",
      success: (result) => {
        if (result.confirm) {
          void this.executeDelete()
        }
      },
    })
  },

  async executeDelete() {
    this.setData({
      deleting: true,
    })

    try {
      await manualAdjustInventory({
        item_name: this.data.originalName,
        category: this.data.originalCategory,
        count: 0,
        remain_level: this.data.remainLevel,
        note: "mini-program manual delete",
      })

      wx.showToast({
        title: "删除成功",
        icon: "success",
      })

      setTimeout(() => {
        wx.navigateBack()
      }, 250)
    } catch (error) {
      wx.showToast({
        title: error instanceof Error ? error.message : "删除失败",
        icon: "none",
      })
    } finally {
      this.setData({
        deleting: false,
      })
    }
  },
})
