playerctl_version_header = configure_file(input: 'playerctl-version.h.in',
  output: 'playerctl-version.h',
  configuration: version_conf)

# Include playerctl_version_header
configuration_inc = include_directories('../')

playerctl_generated = gnome.gdbus_codegen('playerctl-generated', 'mpris-dbus-interface.xml')

headers = [
  'playerctl.h',
  'playerctl-player.h',
  playerctl_version_header]

playerctl_sources = [
  'playerctl-player.c',
  playerctl_generated]

add_global_arguments('-DPLAYERCTL_COMPILATION', language: 'c')

deps = [gobject_dep, gio_dep]

playerctl_shared = shared_library(
  'playerctl-1.0',
  sources: playerctl_sources,
  dependencies: deps,
  version: '0.0.0',
  install: true)

playerctl_static = static_library(
  'playerctl-1.0',
  sources: playerctl_sources,
  dependencies: deps,
  version: '0.0.0',
  install: true)

playerctl_executable = executable(
  'playerctl',
  'playerctl-cli.c',
  dependencies: deps,
  link_with: playerctl_shared,
  include_directories: configuration_inc,
  install: true)

install_headers(headers, install_dir: 'playerctl')

if get_option('introspection')
  gnome.generate_gir(
    playerctl_shared,
    sources: ['playerctl-player.c', 'playerctl-player.h'],
    nsversion: '1.0',
    namespace: 'Playerctl',
    includes : [
      'GObject-2.0',
    ],
    install: true)
endif

pkg.generate(libraries: playerctl_shared,
             subdirs: 'playerctl',
             version: '0.5.0',
             name: 'Playerctl',
             filebase: 'playerctl',
             description: 'A C library for MPRIS players',
             requires: 'gobject-2.0',
             requires_private: 'gio-2.0')
