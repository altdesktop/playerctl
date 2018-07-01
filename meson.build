project(
  'playerctl',
  'c',
  version: '0.6.1',
  meson_version: '>=0.46.0'
)

gnome = import('gnome')
pkgconfig = import('pkgconfig')

version_conf = configuration_data()

playerctl_version = meson.project_version()
version_array = playerctl_version.split('.')

version_conf.set(
  'PLAYERCTL_VERSION',
  meson.project_version(),
)
version_conf.set(
  'PLAYERCTL_MAJOR_VERSION',
  version_array[0].to_int(),
)
version_conf.set(
  'PLAYERCTL_MINOR_VERSION',
  version_array[1].to_int(),
)
version_conf.set(
  'PLAYERCTL_MICRO_VERSION',
  version_array[2].to_int(),
)

gobject_dep = dependency('gobject-2.0', version: '>=2.38')
gio_dep = dependency('gio-unix-2.0')
glib_dep = dependency('glib-2.0')

subdir('playerctl')
subdir('doc')
