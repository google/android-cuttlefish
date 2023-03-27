# Inherit mostly from aosp_cf_x86_64_phone
$(call inherit-product, device/google/cuttlefish/vsoc_x86_64/phone/aosp_cf.mk)
PRODUCT_NAME := aosp_cf_x86_64_ssi

PRODUCT_BUILD_SYSTEM_IMAGE := true
PRODUCT_BUILD_SYSTEM_EXT_IMAGE := true
# Product image is required for now to pass vbmeta_system build. This can be removed once vbmeta_system can be skipped with PRODUCT_BUILD_VBMETA_IMAGE
PRODUCT_BUILD_PRODUCT_IMAGE := true

PRODUCT_BUILD_SYSTEM_OTHER_IMAGE := false
PRODUCT_BUILD_INIT_BOOT_IMAGE := false
PRODUCT_BUILD_RAMDISK_IMAGE := false
PRODUCT_BUILD_VENDOR_IMAGE := false
PRODUCT_BUILD_ODM_IMAGE := false
PRODUCT_BUILD_PRODUCT_SERVICES_IMAGE := false
PRODUCT_BUILD_CACHE_IMAGE := false
PRODUCT_BUILD_USERDATA_IMAGE := false
PRODUCT_BUILD_BOOT_IMAGE := false
PRODUCT_BUILD_VENDOR_BOOT_IMAGE := false
PRODUCT_BUILD_RECOVERY_IMAGE := false
PRODUCT_BUILD_SUPER_PARTITION := false
PRODUCT_BUILD_SUPER_EMPTY_IMAGE := false
PRODUCT_BUILD_VBMETA_IMAGE := false

TARGET_SKIP_OTA_PACKAGE := true
