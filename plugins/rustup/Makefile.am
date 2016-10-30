if ENABLE_RUSTUP_PLUGIN

EXTRA_DIST = $(plugin_DATA)

plugindir = $(libdir)/gnome-builder/plugins
dist_plugin_DATA = \
	rustup.plugin \
	rustup_plugin/__init__.py \
	$(NULL)

resourcedir = $(datadir)/gnome-builder/plugins/
nobase_resource_DATA = \
	rustup_plugin/resources/rustup.sh \
	$(NULL)

gsettings_SCHEMAS = \
	gsettings/org.gnome.builder.plugins.rustup.gschema.xml \
	$(NULL)

.PRECIOUS: $(gsettings_SCHEMAS)

@GSETTINGS_RULES@

EXTRA_DIST += $(gsettings_SCHEMAS)

endif

include $(top_srcdir)/plugins/Makefile.plugin

-include $(top_srcdir)/git.mk
