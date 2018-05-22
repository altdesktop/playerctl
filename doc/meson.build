man_page = configure_file(
  input: 'playerctl.1.in',
  output: 'playerctl.1',
  configuration: version_conf
)
install_man(man_page)

if get_option('gtk-doc')
  gtkdoc = find_program('gtkdoc-scan')
  subdir('reference')
endif
