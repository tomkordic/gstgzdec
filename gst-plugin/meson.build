plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gzdec-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer gzip decompress')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)


zlib_dep = dependency('zlib',
  fallback : ['zlib', 'zlib_dep'],
)

bzip_dep = dependency('bzip2', version : '>=1.0.6',
  fallback : ['bzip2', 'bzip2_dep'],
)

ssl_dep = dependency('openssl')

# gzdec plugin
gstgzdec_sources = [
  'src/gstgzdec.c',
]

# gzdec plugin
gstgzdec_test_sources = [
  'src/tests.c',
]

gstgzdeclib = library('gstgzdec',
 gstgzdec_sources,
 c_args: plugin_c_args,
 dependencies : [gst_dep, gstbase_dep, bzip_dep, zlib_dep],
 install : true,
 install_dir : plugins_install_dir,
)

gstgzdectest = executable('gstgzdec_test',
  gstgzdec_test_sources,
  dependencies : [gst_dep, gstbase_dep, bzip_dep, zlib_dep, ssl_dep],
  link_with : gstgzdeclib
)
