project('autoinit', 'c',
          version: '0.1.2',
    meson_version: '>= 0.50.0',
  default_options: [ 'warning_level=2',
                     'c_std=gnu11',
                   ],
)

top_inc = include_directories('.')

autoinit_prefix = get_option('prefix')
autoinit_libdatadir = join_paths(autoinit_prefix, 'lib/autoinit')
autoinit_dracut_moduledir = join_paths(autoinit_prefix, 'lib/dracut/modules.d')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())

configure_file(
  output: 'autoinit-config.h',
  configuration: config_h,
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

autoinit_sources = [
  'autoinit.c',
]

autoinit = executable('autoinit', autoinit_sources,
  install: true,
  install_dir : autoinit_libdatadir,
)

install_data('dracut/module-setup.sh', install_dir : join_paths(autoinit_dracut_moduledir, '50autoinit'))

meson.add_dist_script('tools/dist.sh', meson.project_version())
