# build TAP-Windows NDIS 6.0 driver

import sys, os, re, shutil, tarfile

import paths

class BuildTAPWindows(object):
    # regex for doing search replace on @MACRO@ style macros
    macro_amper = re.compile(r"@(\w+)@")

    def __init__(self, opt):
        self.opt = opt                                               # command line options
        if not opt.src:
            raise ValueError("source directory undefined")
        self.top = os.path.realpath(opt.src)                         # top-level dir
        self.src = os.path.join(self.top, 'src')                     # src/openvpn dir
        if opt.tapinstall:
            self.top_tapinstall = os.path.realpath(opt.tapinstall)   # tapinstall dir
        else:
            self.top_tapinstall = None

        # path to DDK
        self.ddk_path = paths.DDK

        # driver signing options
        self.sign_cn = opt.cert
        self.crosscert = os.path.join(self.top, opt.crosscert)

        self.inf2cat_cmd = os.path.join(self.ddk_path, 'bin', 'selfsign', 'Inf2Cat')
        self.signtool_cmd = os.path.join(self.ddk_path, 'bin', 'x86', 'SignTool')

        self.timestamp_server = opt.timestamp

    # split a path into a list of components
    @staticmethod
    def path_split(path):
        folders = []
        while True:
            path, folder = os.path.split(path)
            if folder:
                folders.append(folder)
            else:
                if path:
                    folders.append(path)
                break
        folders.reverse()
        return folders

    # run a command
    def system(self, cmd):
        print "RUN:", cmd
        os.system(cmd)

    # make a directory
    def mkdir(self, dir):
        try:
            os.mkdir(dir)
        except:
            pass
        else:
            print "MKDIR", dir

    # make a directory including parents
    def makedirs(self, dir):
        try:
            os.makedirs(dir)
        except:
            pass
        else:
            print "MAKEDIRS", dir

    # copy a file
    def cp(self, src, dest):
        print "COPY %s %s" % (src, dest)
        shutil.copy2(src, dest)

    # make a tarball
    @staticmethod
    def make_tarball(output_filename, source_dir, arcname=None):
        if arcname is None:
            arcname = os.path.basename(source_dir)
        tar = tarfile.open(output_filename, "w:gz")
        tar.add(source_dir, arcname=arcname)
        tar.close()
        print "***** Generated tarball:", output_filename

    # remove a file
    def rm(self, file):
        print "RM", file
        os.remove(file)

    # remove whole directory tree, like rm -rf
    def rmtree(self, dir):
        print "RMTREE", dir
        shutil.rmtree(dir, ignore_errors=True)

    # return path of dist directory
    def dist_path(self):
        return os.path.join(self.top, 'dist')

    # make a distribution directory (if absent) and return its path
    def mkdir_dist(self, x64):
        dir = self.drvdir(self.dist_path(), x64)
        self.makedirs(dir)
        return dir

    # run an MSVC command
    def build_vc(self, cmd):
        self.system('cmd /c "vcvarsall.bat x86 && %s"' % (cmd,))

    # parse version.m4 file
    def parse_version_m4(self):
        kv = {}
        r = re.compile(r'^define\(\[?(\w+)\]?,\s*\[(.*)\]\)')
        with open(os.path.join(self.top, 'version.m4')) as f:
            for line in f:
                line = line.rstrip()
                m = re.match(r, line)
                if m:
                    g = m.groups()
                    kv[g[0]] = g[1]
        return kv

    # our tap-windows version.m4 settings
    def gen_version_m4(self, x64):
        kv = self.parse_version_m4()
        if self.opt.oas:
            kv['PRODUCT_NAME'] = "OpenVPNAS"
            kv['PRODUCT_TAP_WIN_DEVICE_DESCRIPTION'] = "TAP-Win32 Adapter OAS (NDIS 6.0)"
            kv['PRODUCT_TAP_WIN_PROVIDER'] = "TAP-Win32 Provider OAS"
            kv['PRODUCT_TAP_WIN_COMPONENT_ID'] = "tapoas"

        if (x64):
            kv['INF_PROVIDER_SUFFIX'] = ", NTamd64"
            kv['INF_SECTION_SUFFIX'] = ".NTamd64"
        else:
            kv['INF_PROVIDER_SUFFIX'] = ""
            kv['INF_SECTION_SUFFIX'] = ""
        return kv

    # DDK major version number (as a string)
    def ddk_major(self):
        ddk_ver = os.path.basename(self.ddk_path)
        ddk_ver_major = re.match(r'^(\d+)\.', ddk_ver).groups()[0]
        return ddk_ver_major

    # return tapinstall source directory
    def tapinstall_src(self):
        if self.top_tapinstall:
            d = os.path.join(self.top_tapinstall, self.ddk_major())
            if os.path.exists(d):
                return d
            else:
                return self.top_tapinstall

    # preprocess a file, doing macro substitution on @MACRO@
    def preprocess(self, kv, in_path, out_path=None):
        def repfn(m):
            var, = m.groups()
            return kv.get(var, '')
        if out_path is None:
            out_path = in_path
        with open(in_path+'.in') as f:
            modtxt = re.sub(self.macro_amper, repfn, f.read())
        with open(out_path, "w") as f:
            f.write(modtxt)

    # set up configuration files for building tap driver
    def config_tap(self, x64):
        kv = self.gen_version_m4(x64)
        drvdir = self.drvdir(self.src, x64)
        self.mkdir(drvdir)
        self.preprocess(kv, os.path.join(self.src, "OemVista.inf"), os.path.join(drvdir, "OemVista.inf"))
        self.preprocess(kv, os.path.join(self.src, "SOURCES"))
        self.preprocess(kv, os.path.join(self.src, "config.h"))

    # set up configuration files for building tapinstall
    def config_tapinstall(self, x64):
        kv = {}
        tisrc = self.tapinstall_src()
        self.preprocess(kv, os.path.join(tisrc, "sources"))

    # build a "build" file using DDK
    def build_ddk(self, dir, x64, debug):
        setenv_bat = os.path.join(self.ddk_path, 'bin', 'setenv.bat')
        target = 'chk' if debug else 'fre'
        if x64:
            target += ' x64'
        else:
            target += ' x86'

        target += ' wlh'  # vista

        self.system('cmd /c "%s %s %s no_oacr && cd %s && build -cef"' % (
               setenv_bat,
               self.ddk_path,
               target,
               dir
               ))

    # copy tap driver files to dist
    def copy_tap_to_dist(self, x64):
        dist = self.mkdir_dist(x64)
        drvdir = self.drvdir(self.src, x64)
        for dirpath, dirnames, filenames in os.walk(drvdir):
            for f in filenames:
                path = os.path.join(dirpath, f)
                if f.endswith('.inf') or f.endswith('.cat') or f.endswith('.sys'):
                    destfn = os.path.join(dist, f)
                    self.cp(path, destfn)

    # copy tapinstall to dist
    def copy_tapinstall_to_dist(self, x64):
        dist = self.mkdir_dist(x64)
        t = os.path.basename(dist)
        tisrc = self.tapinstall_src()
        for dirpath, dirnames, filenames in os.walk(tisrc):
            if os.path.basename(dirpath) == t:
                for f in filenames:
                    path = os.path.join(dirpath, f)
                    if f == 'tapinstall.exe':
                        destfn = os.path.join(dist, f)
                        self.cp(path, destfn)

    # copy dist-src to dist; dist-src contains prebuilt files
    # for some old platforms (such as win2k)
    def copy_dist_src_to_dist(self):
        dist_path = self.path_split(self.dist_path())
        dist_src = os.path.join(self.top, "dist-src")
        baselen = len(self.path_split(dist_src))
        for dirpath, dirnames, filenames in os.walk(dist_src):
            dirpath_split = self.path_split(dirpath)
            depth = len(dirpath_split) - baselen
            dircomp = ()
            if depth > 0:
                dircomp = dirpath_split[-depth:]
            for exclude_dir in ('.svn', '.git'):
                if exclude_dir in dirnames:
                    dirnames.remove(exclude_dir)
            for f in filenames:
                path = os.path.join(dirpath, f)
                destdir = os.path.join(*(dist_path + dircomp))
                destfn = os.path.join(destdir, f)
                self.makedirs(destdir)
                self.cp(path, destfn)

    # build, sign, and verify tap driver
    def build_tap(self):
        for x64 in (False, True):
            print "***** BUILD TAP x64=%s" % (x64,)
            self.config_tap(x64=x64)
            self.build_ddk(dir=self.src, x64=x64, debug=opt.debug)
            self.sign_verify(x64=x64)
            self.copy_tap_to_dist(x64=x64)

    # build tapinstall
    def build_tapinstall(self):
        for x64 in (False, True):
            print "***** BUILD TAPINSTALL x64=%s" % (x64,)
            tisrc = self.tapinstall_src()
            self.config_tapinstall(x64=x64)
            self.build_ddk(tisrc, x64=x64, debug=opt.debug)
            self.copy_tapinstall_to_dist(x64)

    # build tap driver and tapinstall
    def build(self):
        self.build_tap()
        if self.top_tapinstall:
            self.build_tapinstall()
        self.copy_dist_src_to_dist()

        print "***** Generated files"
        self.dump_dist()

        self.make_tarball(os.path.join(self.top, "tap6.tar.gz"),
                          self.dist_path(),
                          "tap6")

    # like find . | sort
    def enum_tree(self, dir):
        data = []
        for dirpath, dirnames, filenames in os.walk(dir):
            data.append(dirpath)
            for f in filenames:
                data.append(os.path.join(dirpath, f))
        data.sort()
        return data

    # show files in dist
    def dump_dist(self):
        for f in self.enum_tree(self.dist_path()):
            print f

    # remove generated files from given directory tree
    def clean_tree(self, top):
        for dirpath, dirnames, filenames in os.walk(top):
            for d in list(dirnames):
                if d in ('.svn', '.git'):
                    dirnames.remove(d)
                else:
                    path = os.path.join(dirpath, d)
                    deldir = False
                    if d in ('amd64', 'i386', 'dist'):
                        deldir = True
                    if d.endswith('_amd64') or d.endswith('_x86'):
                        deldir = True
                    if deldir:
                        self.rmtree(path)
                        dirnames.remove(d)
            for f in filenames:
                path = os.path.join(dirpath, f)
                if f in ('SOURCES', 'sources', 'config.h'):
                    self.rm(path)
                if f.endswith('.log') or f.endswith('.wrn') or f.endswith('.cod'):
                    self.rm(path)

    # remove generated files for both tap-windows and tapinstall
    def clean(self):
        self.clean_tree(self.top)
        if self.top_tapinstall:
            self.clean_tree(self.top_tapinstall)

    # BEGIN Driver signing

    def drvdir(self, dir, x64):
        if x64:
            return os.path.join(dir, "amd64")
        else:
            return os.path.join(dir, "i386")

    def drvfile(self, x64, ext):
        dd = self.drvdir(self.src, x64)
        for dirpath, dirnames, filenames in os.walk(dd):
            catlist = [ f for f in filenames if f.endswith(ext) ]
            assert(len(catlist)==1)
            return os.path.join(dd, catlist[0])

    def inf2cat(self, x64):
        if x64:
            oslist = "Vista_X64,Server2008_X64,Server2008R2_X64,7_X64"
        else:
            oslist = "Vista_X86,Server2008_X86,7_X86"
        self.system("%s /driver:%s /os:%s" % (self.inf2cat_cmd, self.drvdir(self.src, x64), oslist))

    def sign(self, x64):
            self.system("%s sign /v /ac %s /s my /n %s /t %s %s" % (
                    self.signtool_cmd,
                    self.crosscert,
                    self.sign_cn,
                    self.timestamp_server,
                    self.drvfile(x64, '.cat'),
                ))

    def verify(self, x64):
            self.system("%s verify /kp /v /c %s %s" % (
                    self.signtool_cmd,
                    self.drvfile(x64, '.cat'),
                    self.drvfile(x64, '.sys'),
                ))

    def sign_verify(self, x64):
        self.inf2cat(x64)
        self.sign(x64)
        self.verify(x64)

    # END Driver signing

if __name__ == '__main__':
    # parse options
    import optparse, codecs
    codecs.register(lambda name: codecs.lookup('utf-8') if name == 'cp65001' else None) # windows UTF-8 hack
    op = optparse.OptionParser()

    # defaults
    src = os.path.dirname(os.path.realpath(__file__))
    cert = "openvpn"
    crosscert = "MSCV-VSClass3.cer" # cross certs available here: http://msdn.microsoft.com/en-us/library/windows/hardware/dn170454(v=vs.85).aspx
    timestamp = "http://timestamp.verisign.com/scripts/timstamp.dll"

    op.add_option("-s", "--src", dest="src", metavar="SRC",

                  default=src,
                  help="TAP-Windows top-level directory, default=%s" % (src,))
    op.add_option("--ti", dest="tapinstall", metavar="TAPINSTALL",
                  help="tapinstall (i.e. devcon) source directory (optional)")
    op.add_option("-d", "--debug", action="store_true", dest="debug",
                  help="enable debug build")
    op.add_option("-c", "--clean", action="store_true", dest="clean",
                  help="do an nmake clean before build")
    op.add_option("-b", "--build", action="store_true", dest="build",
                  help="build TAP-Windows and possibly tapinstall (add -c to clean before build)")
    op.add_option("--cert", dest="cert", metavar="CERT",
                  default=cert,
                  help="Common name of code signing certificate, default=%s" % (cert,))
    op.add_option("--crosscert", dest="crosscert", metavar="CERT",
	              default=crosscert,
				  help="The cross-certificate file to use, default=%s" % (crosscert,))
    op.add_option("--timestamp", dest="timestamp", metavar="URL",
                  default=timestamp,
                  help="Timestamp URL to use, default=%s" % (timestamp,))
    op.add_option("-a", "--oas", action="store_true", dest="oas",
                  help="Build for OpenVPN Access Server clients")
    (opt, args) = op.parse_args()

    if len(sys.argv) <= 1:
        op.print_help()
        sys.exit(1)

    btw = BuildTAPWindows(opt)
    if opt.clean:
        btw.clean()
    if opt.build:
        btw.build()
