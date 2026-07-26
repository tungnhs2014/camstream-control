################################################################################
#
# camstream-capture
#
################################################################################

CAMSTREAM_CAPTURE_VERSION = 1.0
CAMSTREAM_CAPTURE_SITE = $(BR2_EXTERNAL_CAMSTREAM_PATH)/../apps/camstream-capture
CAMSTREAM_CAPTURE_SITE_METHOD = local

define CAMSTREAM_CAPTURE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) clean
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CXX="$(TARGET_CXX)"
endef

define CAMSTREAM_CAPTURE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/camstream-capture $(TARGET_DIR)/usr/bin/camstream-capture
endef

$(eval $(generic-package))
