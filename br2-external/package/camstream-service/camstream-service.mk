################################################################################
#
# camstream-service
#
################################################################################

CAMSTREAM_SERVICE_VERSION = 1.0
CAMSTREAM_SERVICE_SITE = $(BR2_EXTERNAL_CAMSTREAM_PATH)/..
CAMSTREAM_SERVICE_SITE_METHOD = local
CAMSTREAM_SERVICE_SUPPORTS_IN_SOURCE_BUILD = NO
CAMSTREAM_SERVICE_CONF_OPTS = \
	-DCAMSTREAM_BUILD_CAPTURE=OFF \
	-DCAMSTREAM_BUILD_GST_TEST=OFF \
	-DCAMSTREAM_BUILD_SERVICE=ON

$(eval $(cmake-package))
