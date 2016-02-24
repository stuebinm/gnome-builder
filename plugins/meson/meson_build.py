from gi.repository import GLib
from gi.repository import Gio
from gi.repository import GObject
from gi.repository import Ide

class MesonBuildSystem(Ide.Object, Ide.BuildSystem, Gio.AsyncInitable):
    project_file = GObject.Property(type=Gio.File, flags=GObject.ParamFlags.CONSTRUCT_ONLY|GObject.ParamFlags.READWRITE)

    def do_init_async(self, priority, cancellable, callback, userdata):
        print("async init")
        task = Gio.Task.new(self, cancellable, callback)
        task.return_boolean(True)

    def do_init_finish(self, result):
        return result.propagate_boolean()

    def do_get_priority(self):
        return -100

    def do_get_builder(self, config):
        return MesonBuilder(context=self.get_context())

    def do_get_build_flags_async(self, file, cancel, callback, userdata):
        pass

    def do_get_build_flags_finish(self, result, error):
        return [None]


class MesonBuilder(Ide.Builder):
    def do_build_async(self, flags, cancellable, callback, result, userdata=None):
        # it's all synchronous for now, we'll make it async when it works
        workdir = self.get_context().get_vcs().get_working_directory()
        print("working in " + workdir.get_path())
        builddir = workdir.get_child('build')
        if not builddir.query_exists():
            builddir.make_directory()
        if not builddir.get_child('build.ninja').query_exists():
            launcher = Ide.SubprocessLauncher.new(Gio.SubprocessFlags.NONE)
            launcher.set_cwd(builddir.get_path())
            launcher.push_args(['meson', workdir.get_path()])
            subproc = launcher.spawn_sync()
            print("running meson")
            subproc.wait()

        launcher = Ide.SubprocessLauncher.new(Gio.SubprocessFlags.NONE)
        launcher.set_cwd(builddir.get_path())
        launcher.push_args(['ninja'])
        subproc = launcher.spawn_sync()
        print("running ninja")
        subproc.wait()

        res = MesonBuildResult()
        task = Gio.Task.new(self, cancellable, callback)
        task.return_pointer(res)
        return (res)

    def do_build_finish(self, result):
        return result.propagate_pointer()


class MesonBuildResult(Ide.BuildResult):
    pass

