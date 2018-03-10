project('playerctl', 'c',
  version: '0.5.0',
  meson_version: '>=0.40.0')

gnome = import('gnome')
pkg = import('pkgconfig')

version_conf = configuration_data()

version_conf.set('PLAYERCTL_VERSION', '0.5.0')
version_conf.set('PLAYERCTL_MAJOR_VERSION', '0')
version_conf.set('PLAYERCTL_MINOR_VERSION', '5')
version_conf.set('PLAYERCTL_MICRO_VERSION', '0')

gobject_dep = dependency('gobject-2.0', version: '>=2.38')
gio_dep = dependency('gio-unix-2.0')

subdir('playerctl')

#if get_option('gtk-doc')
#    gtkdoc = find_program('gtkdoc-scan')
#  subdir('doc')
#endif
