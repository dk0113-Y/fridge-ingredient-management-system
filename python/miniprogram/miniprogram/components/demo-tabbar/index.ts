Component({
  data: {
    homeDotClass: "tab-dot",
    homeLabelClass: "tab-label",
    inventoryDotClass: "tab-dot",
    inventoryLabelClass: "tab-label",
    eventsDotClass: "tab-dot",
    eventsLabelClass: "tab-label",
    pendingDotClass: "tab-dot",
    pendingLabelClass: "tab-label",
  },

  properties: {
    active: {
      type: String,
      value: "home",
      observer(active: string) {
        this.syncActiveState(active)
      },
    },
  },

  lifetimes: {
    attached() {
      this.syncActiveState(this.data.active)
    },
  },

  methods: {
    syncActiveState(active: string) {
      this.setData({
        homeDotClass: active === "home" ? "tab-dot tab-dot-active" : "tab-dot",
        homeLabelClass: active === "home" ? "tab-label tab-label-active" : "tab-label",
        inventoryDotClass: active === "inventory" ? "tab-dot tab-dot-active" : "tab-dot",
        inventoryLabelClass:
          active === "inventory" ? "tab-label tab-label-active" : "tab-label",
        eventsDotClass: active === "events" ? "tab-dot tab-dot-active" : "tab-dot",
        eventsLabelClass: active === "events" ? "tab-label tab-label-active" : "tab-label",
        pendingDotClass: active === "pending" ? "tab-dot tab-dot-active" : "tab-dot",
        pendingLabelClass:
          active === "pending" ? "tab-label tab-label-active" : "tab-label",
      })
    },

    handleGoHome() {
      if (this.data.active === "home") {
        return
      }

      wx.redirectTo({
        url: "/pages/home/index",
      })
    },

    handleGoInventory() {
      if (this.data.active === "inventory") {
        return
      }

      wx.redirectTo({
        url: "/pages/inventory/index",
      })
    },

    handleGoEvents() {
      if (this.data.active === "events") {
        return
      }

      wx.redirectTo({
        url: "/pages/events/index",
      })
    },

    handleGoPending() {
      if (this.data.active === "pending") {
        return
      }

      wx.redirectTo({
        url: "/pages/pending/index",
      })
    },

    handleAdd() {
      wx.navigateTo({
        url: "/pages/inventory-form/index?mode=add",
      })
    },
  },
})
