add_swift_library(swiftos ${SWIFT_SDK_OVERLAY_LIBRARY_BUILD_TYPES} IS_SDK_OVERLAY
  os_log.swift
  os.mm

  SWIFT_COMPILE_FLAGS "${SWIFT_RUNTIME_SWIFT_COMPILE_FLAGS}"
  LINK_FLAGS "${SWIFT_RUNTIME_SWIFT_LINK_FLAGS}"
  SWIFT_MODULE_DEPENDS_OSX Darwin Dispatch ObjectiveC XPC # auto-updated
  SWIFT_MODULE_DEPENDS_IOS Darwin ObjectiveC # auto-updated
  SWIFT_MODULE_DEPENDS_TVOS Darwin ObjectiveC # auto-updated
  SWIFT_MODULE_DEPENDS_WATCHOS Darwin ObjectiveC # auto-updated
)
