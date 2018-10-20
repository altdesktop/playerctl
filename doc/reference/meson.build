glib_prefix = dependency('glib-2.0').get_pkgconfig_variable('prefix')
glib_docpath = join_paths(glib_prefix, 'share', 'gtk-doc', 'html')

configure_file(
  input : 'version.xml.in',
  output : 'version.xml',
  configuration : version_conf,
)

gnome.gtkdoc(
  meson.project_name(),
  src_dir: join_paths(meson.source_root(), 'playerctl'),
  dependencies: [
    glib_dep,
    playerctl_shared_link,
  ],
  mkdb_args: [
    '--output-format=xml',
    '--name-space=' + meson.project_name(),
  ],
  scan_args: '--deprecated-guards="PLAYERCTL_DISABLE_DEPRECATED"',
  gobject_typesfile: join_paths(meson.current_build_dir(), meson.project_name() + '.types'),
  fixxref_args: [
    '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gobject')),
    '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gio')),
    '--extra-dir=@0@'.format(glib_docpath),
  ],
  main_sgml: 'playerctl-docs.xml',
  ignore_headers: [
    'playerctl.h',
    'playerctl-generated.h',
    'playerctl-common.h',
    'playerctl-formatter.h',
  ],
  install: true,
)
