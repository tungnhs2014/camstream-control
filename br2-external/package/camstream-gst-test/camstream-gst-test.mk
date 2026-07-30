################################################################################
#
# camstream-gst-test
#
################################################################################

CAMSTREAM_GST_TEST_VERSION = 1.0
CAMSTREAM_GST_TEST_SITE = $(BR2_EXTERNAL_CAMSTREAM_PATH)/..
CAMSTREAM_GST_TEST_SITE_METHOD = local
CAMSTREAM_GST_TEST_SUPPORTS_IN_SOURCE_BUILD = NO
CAMSTREAM_GST_TEST_DEPENDENCIES = \
	gstreamer1 gst1-plugins-base gst1-plugins-good
CAMSTREAM_GST_TEST_CONF_OPTS = \
	-DCAMSTREAM_BUILD_CAPTURE=OFF \
	-DCAMSTREAM_BUILD_GST_TEST=ON

$(eval $(cmake-package))
