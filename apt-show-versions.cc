/* apt-show-versions.cc - apt-show-versions for APT
 *
 * Copyright (C) 2013 Julian Andres Klode <jak@debian.org>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <apt-pkg/init.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/cacheset.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <set>
#include <fstream>

static pkgPolicy *policy;

static std::string my_name(pkgCache::PkgIterator p, pkgCache::VerIterator c)
{
    std::string name = p.FullName(true);
    std::string my;
    int prio = 0;

    for (auto vf = c.FileList(); c.IsGood(); c++) {
        if (vf.File()->Flags & pkgCache::Flag::NotSource)
            continue;
        else if (!my.empty() && prio >= policy->GetPriority(vf.File()))
            continue;
        else if (vf.File().Codename())
            my = name + "/" + vf.File().Codename();
        else if (vf.File().Archive())
            my = name + "/" + vf.File().Archive();
    }
    return my.empty() ? name : my;
}

static std::ostream& print_only(std::ostream& in)
{
    static std::ofstream nullstream("/dev/null");

    if (!_config->FindB("APT::Show-Versions::Brief"))
        return in;

    in << "\n";
    return nullstream;
}

static void show_help()
{
    std::cout << "apt-show-versions from APT " << pkgVersion << "\n\n";
    std::cout << "Usage:\n";
    std::cout << " apt-show-versions            shows available versions of installed packages\n\n";
    std::cout << "Options:\n";
    std::cout << " -c=?                         configuration file\n";
    std::cout << " -o=?                         option\n";
    std::cout << " -s=?,--status-file=?         select a status file\n";
    std::cout << " -l=?,--list-dir=?            select a lists directory\n";
    std::cout << " -u,--upgradeable             show only upgradeable packages\n";
    std::cout << " -b,--brief                   show package names only\n";
    std::cout << " -h,--help                    show help\n";
}

static void show_upgrade_info(const pkgCache::PkgIterator &p)
{
    if (p->CurrentVer == 0)
        return;

    auto current = p.CurrentVer();
    auto candidate = policy->GetCandidateVer(p);
    auto newer = p.VersionList();

    if (p.VersionList()->NextVer == 0 && current.FileList()->NextFile == 0) {
        if (!_config->FindB("APT::Show-Versions::Upgrades-Only", false))
            std::cout << p.FullName(true) << " " << current.VerStr() << " installed: No available version in archive\n";
    } else if (candidate->ID != current->ID) {
        print_only(std::cout << my_name(p, candidate)) << " upgradable from " << current.VerStr() << " to " << candidate.VerStr() << "\n";
    } else if (current.FileList()->NextFile != 0) {
        /* Still installable */
        if (!_config->FindB("APT::Show-Versions::Upgrades-Only", false))
            print_only(std::cout << my_name(p, candidate)) << " uptodate " << current.VerStr() << "\n";
    } else if (newer.IsGood() && newer->ID != current->ID) {
        /* Not installable version, but newer exists */
        print_only(std::cout << my_name(p, newer)) << " *manually* upgradable from " << current.VerStr() << " to " << newer.VerStr() << "\n";
    } else if (current->NextVer != 0) {
        /* Not installable version, but older exists */
        if (!_config->FindB("APT::Show-Versions::Upgrades-Only", false))
            print_only(std::cout << my_name(p, candidate)) << " " << current.VerStr() << " newer than version in archive\n";
    }
}

int main(int argc,const char **argv)
{
    /* The apt::show-versions::* names might change later on! */
    CommandLine::Args args[] =  {
        {'u',"upgradeable","apt::show-versions::upgrades-only",CommandLine::Boolean},
        {'b',"brief","apt::show-versions::brief",CommandLine::Boolean},
        {'c',"","",CommandLine::ConfigFile},
        {'o',"","",CommandLine::ArbItem},
        {'h',"help","apt::show-versions::help",CommandLine::Boolean},
        {'i',"initialize","apt::show-versions::dummy-option",CommandLine::Boolean},
        {'v',"verbose","apt::show-versions::dummy-option",CommandLine::Boolean},
        {'a',"allversions","apt::show-versions::all-versions",CommandLine::Boolean},
        {0,0,0,0}
    };
    CommandLine cmd(args, _config);
    pkgInitConfig(*_config);
    if (!cmd.Parse(argc, argv)) {
        _error->DumpErrors();
        return 1;
    }
    if (_config->FindB("apt::show-versions::help")) {
        show_help();
        return 0;
    }
    if (_config->FindB("APT::Show-Versions::All-Versions")) {
        std::cerr << "Please use apt-cache policy instead.\n";
        return 1;
    }

    pkgInitSystem(*_config, _system);
    pkgCacheFile cachefile;
    pkgCache *cache = cachefile.GetPkgCache();

    policy = cachefile.GetPolicy();

    if (cache == NULL || _error->PendingError()) {
        _error->DumpErrors();
        return 2;
    }

    if (cmd.FileList[0] == NULL) {
        for (auto p = cache->PkgBegin(); p != cache->PkgEnd(); p++)
            show_upgrade_info(p);
    } else {
        auto pkgs = APT::PackageList::FromCommandLine(cachefile, cmd.FileList);
        for (auto pp = pkgs.begin(); pp != pkgs.end(); pp++)
            show_upgrade_info(*pp);
    }

}
