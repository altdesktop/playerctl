playerctl_version_header = configure_file(
  input: 'playerctl-version.h.in',
  output: 'playerctl-version.h',
  configuration: version_conf,
)

# Include the just generated playerctl_version header
configuration_inc = include_directories('..')

playerctl_generated = gnome.gdbus_codegen(
  'playerctl-generated',
  'mpris-dbus-interface.xml',
)

headers = [
  'playerctl.h',
  'playerctl-player.h',
  playerctl_version_header,
]

playerctl_sources = [
  'playerctl-formatter.c',
  'playerctl-player.c',
  'playerctl-common.c',
  'playerctl-player-manager.c',
  playerctl_generated,
]

# Allow including playerctl.h during compilation
add_global_arguments(
  '-DPLAYERCTL_COMPILATION',
  language: 'c',
)

enums = gnome.mkenums_simple('playerctl-enum-types', sources: headers, install: true)

deps = [
  gobject_dep,
  gio_dep,
]

playerctl_lib = both_libraries(
  'playerctl-1.0',
  playerctl_sources, enums,
  dependencies: deps,
  include_directories: configuration_inc,
  version: '0.0.0',
  install: true,
)

# Required for linking against the shared lib we just built
playerctl_shared_link = declare_dependency(
  link_with: playerctl_lib.get_shared_lib(),
  dependencies: deps,
)

playerctl_executable = executable(
  'playerctl',
  'playerctl-cli.c', enums,
  dependencies: playerctl_shared_link,
  include_directories: configuration_inc,
  install: true,
)

install_headers(
  headers,
  install_dir: join_paths(get_option('includedir'), 'playerctl'),
)

if get_option('introspection')
  # The below isn't strictly required, since meson checks for gobject-introspection anyway when
  # we call gnome.generate_gir. However, doing it this way we have a little nicer error reporting
  # in case the user enabled instropection but doesn't have gobject-introspection installed.
  introspection_dep = dependency('gobject-introspection-1.0', required: false)
  if not introspection_dep.found()
      error('You need to have gobject-introspection installed to generate Gir data. Disable it with `-Dintrospection=false` if you don\'t want to build it')
  endif

  gnome.generate_gir(
    playerctl_lib.get_shared_lib(),
    sources: [
      enums,
      'playerctl-player-manager.c',
      'playerctl-player-manager.h',
      'playerctl-player.c',
      'playerctl-player.h',
    ],
    nsversion: '1.0',
    namespace: 'Playerctl',
    includes : ['GObject-2.0', 'Gio-2.0'],
    install: true,
  )
endif

pkgconfig.generate(
  libraries: playerctl_lib.get_shared_lib(),
  subdirs: 'playerctl',
  version: meson.project_version(),
  name: 'Playerctl',
  filebase: 'playerctl-1.0',
  description: 'A C library for MPRIS players',
  requires: ['gobject-2.0', 'gio-2.0'],
)
