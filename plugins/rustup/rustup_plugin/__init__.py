#!/usr/bin/env python3

#
# __init__.py
#
# Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import gi
import os

gi.require_version('Ide', '1.0')
gi.require_version('Gtk', '3.0')

from gi.repository import GLib
from gi.repository import GObject
from gi.repository import Gio
from gi.repository import Gtk
from gi.repository import Ide
from gi.repository import Peas

_ = Ide.gettext

def get_module_data_path(name):
    engine = Peas.Engine.get_default()
    plugin = engine.get_plugin_info('rustup_plugin')
    data_dir = plugin.get_data_dir()
    return GLib.build_filenamev([data_dir, name])

class RustupApplicationAddin(GObject.Object, Ide.ApplicationAddin):
    """
    The RustupApplicationAddin provides us a single point to manage updates
    within the Builder process. Our other addins will perform their various
    actions by communicating with this singleton.
    """
    # Our singleton instance
    instance = None

    # Current updater while we are busy
    updater = None

    @GObject.Property(type=bool, default=False)
    def busy(self):
        return self.updater is not None

    def do_load(self, application):
        RustupApplicationAddin.instance = self

    def do_unload(self, application):
        RustupApplicationAddin.instance = None

    def check_update(self, *args):
        """
        This function will begin checking the rustup service for updates
        to Rust. If we find one, we will notify listeners that we have
        started a new transfer (and they can add it to their particular
        IdeTransferManager.
        """
        settings = Gio.Settings.new('org.gnome.builder.plugins.rustup')

        channel = settings.get_string('channel')
        prefix = settings.get_string('prefix')

        self.updater = RustupUpdater(channel=channel, prefix=prefix)
        self.updater.run()

        self.notify("busy")

    def cancel(self):
        if self.updater:
            self.updater.cancel()

class RustupUpdater(GObject.Object):
    """
    The RustUpdater class handles the process of checking for a new version
    of rust as well as tracking the transfer progress of the process.

    Compare this to RustupTransfer() which is a wrapper object around the
    updater so that we can see the transfer in all workbench windows but
    only a single update process for the process.
    """
    channel = GObject.Property(type=str, default='stable')
    prefix = GObject.Property(type=str, default='~/.local/')

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cancellable = Gio.Cancellable()

    def cancel(self):
        self.cancellable.cancel()

    def run(self):
        rustup_sh_path = get_module_data_path('resources/rustup.sh')
        prefix = os.path.expanduser(self.props.prefix)

        launcher = Ide.SubprocessLauncher()
        launcher.set_flags(Gio.SubprocessFlags.STDOUT_PIPE |
                           Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_run_on_host(True)
        launcher.set_clear_env(False)
        launcher.push_argv(rustup_sh_path)
        launcher.push_argv('--disable-sudo')
        launcher.push_argv('--yes')
        launcher.push_argv('--save')
        launcher.push_argv('--channel=' + self.props.channel)
        launcher.push_argv('--prefix=' + prefix)

        subprocess = launcher.spawn()

        stdout_pipe = subprocess.get_stdout_pipe()
        data_stream = Gio.DataInputStream.new(stdout_pipe)
        data_stream.read_line_async(GLib.PRIORITY_DEFAULT, self.cancellable, self._read_line_cb)

        subprocess.wait_check_async(self.cancellable, self._wait_cb)

    def _wait_cb(self, subprocess, result):
        print('>>> subprocess exited')
        try:
            subprocess.wait_check_finish(result)
            print("Finished cleanly")
        except Exception as ex:
            print(ex)

    def _read_line_cb(self, data_stream, result):
        try:
            line, length = data_stream.read_line_finish_utf8(result)
            print(">>>", line)
            if length > 0:
                data_stream.read_line_async(GLib.PRIORITY_DEFAULT, self.cancellable, self._read_line_cb)
        except Exception as ex:
            print(ex)

class RustupPreferencesAddin(GObject.Object, Ide.PreferencesAddin):

    def do_load(self, preferences):
        preferences.add_page('sdk', _('SDKs'), 550)
        preferences.add_list_group('sdk', 'rustup', _('Rust Developer Channel'), Gtk.SelectionMode.SINGLE, 100)

        updater = Gtk.Button(halign=Gtk.Align.END, label=_("Check for updates"), visible=True)
        updater.connect('clicked', RustupApplicationAddin.instance.check_update)
        RustupApplicationAddin.instance.bind_property('busy', updater, 'sensitive',
            GObject.BindingFlags.SYNC_CREATE | GObject.BindingFlags.INVERT_BOOLEAN)

        self.ids = [
            preferences.add_radio('sdk', 'rustup',
                'org.gnome.builder.plugins.rustup', 'channel', None, "'stable'", _('Stable'),
                _('The current stable release of Rust, updated every six weeks and backwards-compatible'),
                None, 0),
            preferences.add_radio('sdk', 'rustup',
                'org.gnome.builder.plugins.rustup', 'channel', None, "'beta'", _('Beta'),
                _('A preview of the upcoming stable release, intended for testing by crate authors. Updated every six weeks and as needed'),
                None, 0),
            preferences.add_radio('sdk', 'rustup',
                'org.gnome.builder.plugins.rustup', 'channel', None, "'nightly'", _('Nightly'),
                _('The current development branch. It includes unstable features that are not available in the betas or stable releases.'),
                None, 0),
            preferences.add_switch('sdk', 'rustup',
                'org.gnome.builder.plugins.rustup', 'auto-update', None, "true", _('Automatically Update'),
                _('Use RustUp to automatically keep your rust installation up to date.'),
                None, 0),
            preferences.add_custom('sdk', 'rustup', updater, None, 1000),
        ]

    def do_unload(self, preferences):
        if self.ids:
            for id in self.ids:
                preferences.remove_id(id)

class RustupTransfer(Ide.Object, Ide.Transfer):

    title = GObject.Property(type=str)
    icon_name = GObject.Property(type=str)
    progress = GObject.Property(type=float)
    status = GObject.Property(type=str)

    def complete(self, task):
        task.return_boolean(True)

    def do_execute_async(self, cancellable, callback, data):
        task = Gio.Task.new(self, cancellable, callback)
        GLib.timeout_add_seconds(10, self.complete, task)

    def do_execute_finish(self, task):
        return task.propagate_boolean()

