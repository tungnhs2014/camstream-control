################################################################################
#
# camstream-gst-test
#
################################################################################

CAMSTREAM_GST_TEST_VERSION = 1.0
CAMSTREAM_GST_TEST_SITE = $(BR2_EXTERNAL_CAMSTREAM_PATH)/../apps/camstream-gst-test
CAMSTREAM_GST_TEST_SITE_METHOD = local
CAMSTREAM_GST_TEST_DEPENDENCIES = \
	gstreamer1 gst1-plugins-base gst1-plugins-good

define CAMSTREAM_GST_TEST_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) clean
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CAMSTREAM_GST_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/camstream-gst-test \
		$(TARGET_DIR)/usr/bin/camstream-gst-test
endef

$(eval $(generic-package))
