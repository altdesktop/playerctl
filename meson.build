project(
  'playerctl',
  'c',
  version: '2.2.1',
  meson_version: '>=0.50.0'
)

release_date = 'August 8, 2020'

gnome = import('gnome')
pkgconfig = import('pkgconfig')

datadir = get_option('datadir')
bindir = get_option('bindir')
prefix = get_option('prefix')

version_conf = configuration_data()

playerctl_version = meson.project_version().split('-')[0]
version_array = playerctl_version.split('.')
playerctl_major_version = version_array[0]

version_conf.set(
  'PLAYERCTL_VERSION',
  meson.project_version(),
)
version_conf.set(
  'PLAYERCTL_MAJOR_VERSION',
  playerctl_major_version.to_int(),
)
version_conf.set(
  'PLAYERCTL_MINOR_VERSION',
  version_array[1].to_int(),
)
version_conf.set(
  'PLAYERCTL_MICRO_VERSION',
  version_array[2].to_int(),
)
version_conf.set(
  'PLAYERCTL_RELEASE_DATE',
  release_date,
)

gobject_dep = dependency('gobject-2.0', version: '>=2.38')
gio_dep = dependency('gio-unix-2.0')
glib_dep = dependency('glib-2.0')
bash_comp = dependency('bash-completion', required: false)

subdir('playerctl')
subdir('data')
subdir('doc')
