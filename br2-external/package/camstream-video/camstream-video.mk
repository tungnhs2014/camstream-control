################################################################################
#
# camstream-video
#
################################################################################

CAMSTREAM_VIDEO_VERSION = 1.0
CAMSTREAM_VIDEO_SITE = $(BR2_EXTERNAL_CAMSTREAM_PATH)/../drivers/camstream-video
CAMSTREAM_VIDEO_SITE_METHOD = local
CAMSTREAM_VIDEO_MODULE_MAKE_OPTS = \
	KCPPFLAGS="-fmacro-prefix-map=$(LINUX_DIR)=."

$(eval $(kernel-module))
$(eval $(generic-package))
