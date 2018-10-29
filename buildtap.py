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
            if opt.package:
                raise ValueError("parameter -p must be used with --ti")

        if opt.sdk == "ewdk":
            # path to EWDK
            self.ewdk_path = paths.EWDK
            self.ewdk_cmd = os.path.join(self.ewdk_path, "BuildEnv", "SetupBuildEnv.cmd")

        # path to makensis
        self.makensis = os.path.join(paths.NSIS, 'makensis.exe')

        # Driver Kit build system strings
        # This driver builds for a specific set of architectures.
        # The driver kit build system has a set of architecture-specific paths.
        # The installation script has a set of architecture-specific paths.
        # The driver kit build system has a set of architecture-specific parameters.
        # architecture -> build system parameter map
        self.architecture_platform_map = {"i386": "Win32", "amd64": "x64"}
        # architecture -> build system folder name fragment map
        self.architecture_platform_folder_map = {"i386": "", "amd64": "x64"}
        # supported arch names, also installation script folder names
        self.architectures_supported = self.architecture_platform_map.keys()
        # Release vs Debug
        self.configuration = 'Debug' if opt.debug else 'Release'

        # driver signing options
        self.codesign = opt.codesign
        self.sign_cn = opt.cert
        self.sign_cert = opt.certfile
        self.cert_pw = opt.certpw
        self.crosscert = os.path.join(self.top, opt.crosscert)

        self.inf2cat_cmd = 'Inf2Cat.exe'
        self.signtool_cmd = 'SignTool.exe'

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
        result = os.system(cmd)
        if result != 0:
            raise ValueError("command failed")

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

    # return path of dist include directory
    def dist_include_path(self):
        return os.path.join(self.dist_path(), 'include')

    # make a distribution directory (if absent) and return its path
    def mkdir_dist(self, arch):
        dir = os.path.join(self.dist_path(), arch)
        self.makedirs(dir)
        return dir

    # run an (E)WDK command
    def run_ewdk(self, cmd):
        if opt.sdk == "ewdk":
            # In EWDK case, run the command after setting the building environment.
            self.system('cmd /c ""%s" && %s"' % (self.ewdk_cmd, cmd))
        else:
            # In WDK case, run the command directly, as the building environment should be set
            # within Visual Studio Command Prompt already.
            self.system(cmd)

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
    def gen_version_m4(self):
        kv = self.parse_version_m4()
        if self.opt.oas: # for OpenVPN Connect (i.e. OpenVPN Access Server)
            kv['PRODUCT_NAME'] = "OpenVPNAS"
            kv['PRODUCT_TAP_WIN_DEVICE_DESCRIPTION'] = "TAP Adapter OAS NDIS 6.0"
            kv['PRODUCT_TAP_WIN_PROVIDER'] = "TAP-Win32 Provider OAS"
            kv['PRODUCT_TAP_WIN_COMPONENT_ID'] = "tapoas"

        return kv

    # return tapinstall source directory
    def tapinstall_src(self):
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
    def config_tap(self):
        kv = self.gen_version_m4()
        self.preprocess(kv, os.path.join(self.src, "OemVista.inf"))
        self.preprocess(kv, os.path.join(self.src, "tap-windows6.vcxproj"))
        self.preprocess(kv, os.path.join(self.src, "config.h"))

    # build a "msbuild" file using (E)WDK
    def build_ewdk(self, project_file, arch):
        self.run_ewdk('msbuild.exe %s /p:Configuration=%s /p:Platform=%s' % (
               project_file,
               self.configuration,
               self.architecture_platform_map[arch]
               ))

    # copy tap driver files to dist
    def copy_tap_to_dist(self, arch):
        dist = self.mkdir_dist(arch)
        drvdir = self.drvdir(self.src, arch)
        for dirpath, dirnames, filenames in os.walk(drvdir):
            for f in filenames:
                path = os.path.join(dirpath, f)
                if f.endswith('.inf') or f.endswith('.cat') or f.endswith('.sys'):
                    destfn = os.path.join(dist, f)
                    self.cp(path, destfn)

    # copy tap-windows.h to dist/include
    def copy_include(self):
        incdir = os.path.join(self.dist_path(), 'include')
        self.makedirs(incdir)
        self.cp(os.path.join(self.src, 'tap-windows.h'), incdir)

    # copy tapinstall to dist
    def copy_tapinstall_to_dist(self, arch):
        self.cp(self.tifile_src(arch), self.tifile_dst(arch))

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
        print "***** BUILD TAP config"
        self.config_tap()
        project_file = os.path.join(self.src, "tap-windows6.vcxproj")
        for arch in self.architectures_supported:
            print "***** BUILD TAP arch=%s" % (arch,)
            self.build_ewdk(project_file=project_file, arch=arch)
            self.copy_tap_to_dist(arch=arch)
            if self.codesign:
                self.sign_verify(arch=arch)

    # build tapinstall
    def build_tapinstall(self):
        project_file = os.path.join(self.tapinstall_src(), "devcon.sln")
        using_prebuilt = not os.path.isfile(project_file)

        for arch in self.architectures_supported:
            print "***** BUILD TAPINSTALL arch=%s" % (arch,)
            if using_prebuilt:
                print "***** BUILD TAPINSTALL - devcon solution file not found; relying on prebuilt binary"
            else:
                self.build_ewdk(project_file=project_file, arch=arch)
            self.copy_tapinstall_to_dist(arch)
            if self.codesign:
                self.sign_verify_ti(arch=arch)

    # build tap driver and tapinstall
    def build(self):
        self.build_tap()
        self.copy_include()
        if self.top_tapinstall:
            self.build_tapinstall()
        self.copy_dist_src_to_dist()

        print "***** Generated files"
        self.dump_dist()

        tapbase = "tapoas6" if self.opt.oas else "tap6"
        self.make_tarball(os.path.join(self.top, tapbase+".tar.gz"),
                          self.dist_path(),
                          tapbase)

    # package the produced files into an NSIS installer
    def package(self):

        # Generate license.txt and converting LF -> CRLF as we go. Apparently
        # this type of conversion will stop working in Python 3.x.
        dst = open(os.path.join(self.dist_path(), 'license.txt'), mode='wb')

        for f in (os.path.join(self.top, 'COPYING'), os.path.join(self.top, 'COPYRIGHT.GPL')):
            src=open(f, mode='rb')
            dst.write(src.read()+'\r\n')
            src.close()

        dst.close()

        # Copy tap-windows.h to dist include directory
        self.mkdir(self.dist_include_path())
        self.cp(os.path.join(self.src, 'tap-windows.h'), self.dist_include_path())

        # Get variables from version.m4
        kv = self.gen_version_m4()

        installer_type = ""
        if self.opt.oas:
            installer_type = "-oas"
        installer_file=os.path.join(self.top, 'tap-windows'+installer_type+'-'+kv['PRODUCT_VERSION']+'-I'+kv['PRODUCT_TAP_WIN_BUILD']+'.exe')

        installer_cmd = "\"\"%s\" -DDEVCON32=%s -DDEVCON64=%s -DDEVCON_BASENAME=%s -DPRODUCT_TAP_WIN_COMPONENT_ID=%s -DPRODUCT_NAME=%s -DPRODUCT_PUBLISHER=\"%s\" -DPRODUCT_VERSION=%s -DPRODUCT_TAP_WIN_BUILD=%s -DOUTPUT=%s -DIMAGE=%s %s\"" % \
                        (self.makensis,
                         self.tifile_dst(arch="i386"),
                         self.tifile_dst(arch="amd64"),
                         'tapinstall.exe',
                         kv['PRODUCT_TAP_WIN_COMPONENT_ID'],
                         kv['PRODUCT_NAME'],
                         kv['PRODUCT_PUBLISHER'],
                         kv['PRODUCT_VERSION'],
                         kv['PRODUCT_TAP_WIN_BUILD'],
                         installer_file,
                         self.dist_path(),
                         os.path.join(self.top, 'installer', 'tap-windows6.nsi')
                        )

        self.system(installer_cmd)
        if self.codesign:
            self.sign(installer_file)

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
                if d in ('.svn', '.git', '.vs'):
                    dirnames.remove(d)
                else:
                    path = os.path.join(dirpath, d)
                    deldir = False
                    if d in ('x64', 'Debug', 'Release', 'dist'):
                        deldir = True
                    if deldir:
                        self.rmtree(path)
                        dirnames.remove(d)
            for f in filenames:
                path = os.path.join(dirpath, f)
                # Generated files
                if f in ('OemVista.inf', 'tap-windows6.vcxproj', 'config.h'):
                    self.rm(path)
                # Stray VS file
                if f in ('tap-windows6.sln'):
                    self.rm(path)
                # Build outputs
                if f.endswith('.log') or f.endswith('.wrn') or f.endswith('.cod'):
                    self.rm(path)

    # remove generated files for both tap-windows and tapinstall
    def clean(self):
        self.clean_tree(self.top)
        if self.top_tapinstall:
            self.clean_tree(self.top_tapinstall)

    # Calculate tapinstall.exe file names
    def tifile_src(self, arch):
        return os.path.join(self.tapinstall_src(), self.architecture_platform_folder_map[arch], self.configuration, 'devcon.exe')

    def tifile_dst(self, arch):
        return os.path.join(self.top, "dist", arch, 'tapinstall.exe')

    # BEGIN Driver signing

    def drvdir(self, dir, arch):
        return os.path.join(dir, self.architecture_platform_folder_map[arch], self.configuration, 'tap-windows6')

    def drvfile(self, arch, ext):
        dd = self.drvdir(self.src, arch)
        for dirpath, dirnames, filenames in os.walk(dd):
            catlist = [ f for f in filenames if f.endswith(ext) ]
            assert(len(catlist)==1)
            return os.path.join(dd, catlist[0])

    def inf2cat(self, arch):
        if arch == "amd64":
            oslist = "Vista_X64,Server2008_X64,Server2008R2_X64,7_X64"
        elif arch == "i386":
            oslist = "Vista_X86,Server2008_X86,7_X86"
        else:
            print "ERROR: inf2cat OS list not known for architecture %s!!" % (arch)
        self.run_ewdk("%s /driver:%s /os:%s" % (self.inf2cat_cmd, self.mkdir_dist(arch), oslist))

    def sign(self, file):
        certspec = ""
        if self.sign_cert:
            certspec += "/f '%s' " % self.sign_cert
            if self.cert_pw:
                certspec += "/p '%s' " % self.cert_pw
        else:
            certspec += "/s my /n %s " % self.sign_cn

        self.run_ewdk("%s sign /v /ac %s %s /t %s %s" % (
                self.signtool_cmd,
                self.crosscert,
                certspec,
                self.timestamp_server,
                file,
            ))

    def sign_driver(self, arch):
        self.sign(self.drvfile(arch, '.cat'))

    def verify(self, arch):
            self.run_ewdk("%s verify /kp /v /c %s %s" % (
                    self.signtool_cmd,
                    self.drvfile(arch, '.cat'),
                    self.drvfile(arch, '.sys'),
                ))

    def sign_verify(self, arch):
        self.inf2cat(arch)
        self.sign_driver(arch)
        self.verify(arch)

    def sign_verify_ti(self, arch):
        self.sign(self.tifile_dst(arch))
        self.run_ewdk("%s verify /pa %s" % (self.signtool_cmd, self.tifile_dst(arch)))

    # END Driver signing

if __name__ == '__main__':
    # parse options
    import optparse, codecs
    codecs.register(lambda name: codecs.lookup('utf-8') if name == 'cp65001' else None) # windows UTF-8 hack
    op = optparse.OptionParser()

    # defaults
    src = os.path.dirname(os.path.realpath(__file__))
    sdk = "ewdk"
    cert = "openvpn"
    crosscert = "MSCV-VSClass3.cer" # cross certs available here: http://msdn.microsoft.com/en-us/library/windows/hardware/dn170454(v=vs.85).aspx
    timestamp = "http://timestamp.verisign.com/scripts/timstamp.dll"

    op.add_option("-s", "--src", dest="src", metavar="SRC",

                  default=src,
                  help="TAP-Windows top-level directory, default=%s" % (src,))
    op.add_option("--ti", dest="tapinstall", metavar="TAPINSTALL",
                  help="tapinstall (i.e. devcon) directory (optional)")
    op.add_option("-d", "--debug", action="store_true", dest="debug",
                  help="enable debug build")
    op.add_option("-c", "--clean", action="store_true", dest="clean",
                  help="do an nmake clean before build")
    op.add_option("-b", "--build", action="store_true", dest="build",
                  help="build TAP-Windows and possibly tapinstall (add -c to clean before build)")
    op.add_option("--sdk", dest="sdk", metavar="SDK",
                  default=sdk,
                  help="SDK to use for building: ewdk or wdk, default=%s" % (sdk,))
    op.add_option("--sign", action="store_true", dest="codesign",
                  default=False, help="sign the driver files")
    op.add_option("-p", "--package", action="store_true", dest="package",
                  help="generate an NSIS installer from the compiled files")
    op.add_option("--cert", dest="cert", metavar="CERT",
                  default=cert,
                  help="Common name of code signing certificate, default=%s" % (cert,))
    op.add_option("--certfile", dest="certfile", metavar="CERTFILE",
                  help="Path to the code signing certificate")
    op.add_option("--certpw", dest="certpw", metavar="CERTPW",
                  help="Password for the code signing certificate/key (optional)")
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
    if opt.package:
        btw.package()
