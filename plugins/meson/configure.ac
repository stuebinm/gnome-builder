AC_ARG_ENABLE([meson-plugin],
              [AS_HELP_STRING([--enable-meson-plugin=@<:@yes/no@:>@],
                              [Build with support for the Meson build system])],
              [enable_meson_plugin=$enableval],
              [enable_meson_plugin=yes])

AM_CONDITIONAL(ENABLE_MESON_PLUGIN, test x$enable_meson_plugin != xno)

AC_CONFIG_FILES([plugins/meson/Makefile])
