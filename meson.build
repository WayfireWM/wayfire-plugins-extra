project(
	'wayfire-plugins-extra',
	'c',
	'cpp',
	version: '0.8.0',
	license: 'MIT',
	meson_version: '>=0.51.0',
	default_options: [
		'cpp_std=c++17',
		'c_std=c11',
		'warning_level=2',
		'werror=false',
	],
)

wayfire = dependency('wayfire')
giomm = dependency('giomm-2.4')

if get_option('enable_dbus') == true
    dbus = subproject('dbus')
endif

if get_option('enable_windecor') == true
    windecor = subproject('windecor')
endif

if get_option('enable_wayfire_shadows') == true
    wayfire_shadows = subproject('wayfire-shadows')
endif

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic'], language:'cpp')

add_project_arguments(['-Wno-unused-parameter'], language: 'cpp')
subdir('proto')
subdir('src')
subdir('metadata')

summary = [
	'',
	'----------------',
	'wayfire-plugins-extra @0@'.format(meson.project_version()),
	'----------------',
	''
]
message('\n'.join(summary))
