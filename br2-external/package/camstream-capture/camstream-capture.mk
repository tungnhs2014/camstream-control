################################################################################
#
# camstream-capture
#
################################################################################

CAMSTREAM_CAPTURE_VERSION = 1.0
CAMSTREAM_CAPTURE_SITE = $(BR2_EXTERNAL_CAMSTREAM_PATH)/..
CAMSTREAM_CAPTURE_SITE_METHOD = local
CAMSTREAM_CAPTURE_SUPPORTS_IN_SOURCE_BUILD = NO
CAMSTREAM_CAPTURE_CONF_OPTS = \
	-DCAMSTREAM_BUILD_CAPTURE=ON \
	-DCAMSTREAM_BUILD_GST_TEST=OFF \
	-DCAMSTREAM_BUILD_SERVICE=OFF

$(eval $(cmake-package))
