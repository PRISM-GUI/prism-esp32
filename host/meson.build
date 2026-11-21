# Host (native) tests of the board code that is pure logic: the display's CPU conversion.
# A separate Meson project, so ESP-IDF's cross build never applies to it.
#
#   meson setup host-build host && meson test -C host-build
project('prism-esp32-host', 'cpp',
  default_options : ['cpp_std=c++20', 'warning_level=2'],
  meson_version : '>= 1.5.0',
)

# The repository root, normalized: Meson refuses 'host/..' as an include directory.
repo_inc = include_directories(import('fs').parent(meson.current_source_dir()))
doctest_dep = dependency('doctest', fallback : ['doctest', 'doctest_dep'])

test('test_rgb565', executable('test_rgb565', files('test_rgb565.cpp'),
  include_directories : repo_inc,
  dependencies : doctest_dep))
