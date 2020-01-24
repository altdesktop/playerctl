application_id = 'org.mpris.MediaPlayer2.playerctld'

service_conf = configuration_data()
service_conf.set('application_id', application_id)
service_conf.set('bindir', bindir)
service_conf.set('prefix', prefix)
service = application_id + '.service'

configure_file(
  input: service + '.in',
  output: service,
  install: true,
  install_dir: join_paths(datadir, 'dbus-1', 'services'),
  configuration: service_conf
)

if get_option('bash-completions')
	bash_files = files(
		'playerctl.bash',
	)
	if bash_comp.found()
		bash_install_dir = bash_comp.get_pkgconfig_variable('completionsdir')
	else
		bash_install_dir = join_paths(datadir, 'bash-completion', 'completions')
	endif

	install_data(bash_files, install_dir: bash_install_dir)
endif
