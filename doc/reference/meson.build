glib_prefix = dependency('glib-2.0').get_pkgconfig_variable('prefix')
glib_html_docpath = join_paths(glib_prefix, 'share', 'gtk-doc', 'html')
gobject_docpath = join_paths(glib_html_docpath, 'gobject')
gio_docpath = join_paths(glib_html_docpath, 'gio')
glib_docpath = join_paths(glib_html_docpath, 'glib')

version_xml = configure_file(input : 'version.xml.in',
  output : 'version.xml',
  configuration : version_conf)

gnome.gtkdoc('playerctl',
  src_dir: 'playerctl',
  fixxref_args: [
    '--extra-dir=@0@'.format(gobject_docpath),
    '--extra-dir=@0@'.format(gio_docpath),
    '--extra-dir=@0@'.format(glib_docpath),
  ],
  scan_args: [
    '--deprecated-guards="PLAYERCTL_DISABLE_DEPRECATED"',
  ],
  mkdb_args: [
    '--sgml-mode',
    '--output-format=xml',
    '--name-space=playerctl',
  ],
  dependencies: gobject_dep,
  ignore_headers: [
    'playerctl.h',
    'playerctl-version.h',
  ],
  main_sgml: 'playerctl-docs.xml',
  install: true)
