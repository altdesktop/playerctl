man_page = configure_file(
  input: 'playerctl.1.in',
  output: 'playerctl.1',
  configuration: version_conf,
)
install_man(man_page)

if get_option('gtk-doc')
  gtkdoc = find_program('gtkdoc-scan', required: false)
  if not gtkdoc.found()
      error('You need to have gtk-doc installed to generate docs. Disable it with `-Dgtk-doc=false` if you don\'t want to build docs')
  endif
  subdir('reference')
endif
