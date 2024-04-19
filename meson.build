project('ostreeinit', 'c',
          version: '0.1.1',
    meson_version: '>= 0.50.0',
  default_options: [ 'warning_level=2',
                     'c_std=gnu11',
                   ],
)

top_inc = include_directories('.')

ostreeinit_prefix = get_option('prefix')
ostreeinit_libdatadir = join_paths(ostreeinit_prefix, 'lib/ostreeinit')
ostreeinit_dracut_moduledir = join_paths(ostreeinit_prefix, 'lib/dracut/modules.d')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set('DEBUG_PRINT', get_option('debug_print'))

configure_file(
  output: 'ostreeinit-config.h',
  configuration: config_h,
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

ostreeinit_sources = [
  'ostreeinit.c',
]

ostreeinit = executable('ostreeinit', ostreeinit_sources,
  install: true,
  install_dir : ostreeinit_libdatadir,
)

install_data('dracut/module-setup.sh', install_dir : join_paths(ostreeinit_dracut_moduledir, '50ostreeinit'))

meson.add_dist_script('tools/dist.sh', meson.project_version())
