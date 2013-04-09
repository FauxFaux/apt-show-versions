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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <set>
#include <vector>
#include <array>
#include <fstream>
#include <iomanip>

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
    std::cout << "apt-show-versions 0.30 using APT " << pkgVersion << "\n\n";
    std::cout << "Usage:\n";
    std::cout << " apt-show-versions            shows available versions of installed packages\n\n";
    std::cout << "Options:\n";
    std::cout << " -c=?                         configuration file\n";
    std::cout << " -o=?                         option\n";
    std::cout << " -R,--regex-all               regular expressions apply to uninstalled packages\n";
    std::cout << " -u,--upgradeable             show only upgradeable packages\n";
    std::cout << " -b,--brief                   show package names only\n";
    std::cout << " -h,--help                    show help\n";
}

template<size_t N> struct TablePrinter {
    typedef std::array<std::string, N>  Line;
    std::vector<Line> lines;
    size_t max[N];

    TablePrinter() {
        for (size_t i = 0; i < N; i++)
            max[i] = 0;
    }

    void insert(Line &line) {
        for (size_t i = 0; i < N; i++)
            max[i] = line[i].size() > max[i] ? line[i].size() : max[i];

        lines.push_back(line);
    }

    void output() {
        std::cout.setf(std::ios::left);
        for (auto line = lines.begin(); line != lines.end(); line++) {
            for (size_t i = 0; i < N; i++)
                std::cout << std::setw(max[i] + (i < N - 1)) << (*line)[i];

            std::cout << "\n";
        }
    }
};

static void describe_state(const pkgCache::PkgIterator &pkg)
{
    static const char *selections[] = {"unknown", "install", "hold",
                                      "deinstall", "purge"};
    static const char *installs[] = {"ok", "reinst-required", "hold-install",
                                     "hold-reinst-required"};
    static const char *currents[] = {"not-installed", "unpacked",
                                     "half-configured", "INVALID",
                                     "half-installed", "config-files",
                                     "installed", "triggers-awaited",
                                     "triggers-pending"};

    assert(pkg->SelectedState < sizeof(selections) / sizeof(selections[0]));
    assert(pkg->InstState < sizeof(installs) / sizeof(installs[0]));
    assert(pkg->CurrentState < sizeof(currents) / sizeof(currents[0]));

    std::cout << " " << selections[pkg->SelectedState]
              << " " << installs[pkg->InstState]
              << " " << currents[pkg->CurrentState];
}

static void show_all_versions(const pkgCache::PkgIterator &pkg)
{
    TablePrinter<4> table;

    if (pkg->CurrentVer) {
        std::cout << pkg.FullName(true) << " " << pkg.CurrentVer().VerStr();
        describe_state(pkg);
        std::cout << "\n";
    } else {
        std::cout << "Not installed\n";
    }


    for (auto ver = pkg.VersionList(); ver.IsGood(); ver++) {
        for (auto vf = ver.FileList(); vf.IsGood(); vf++) {
            if (vf.File()->Flags & pkgCache::Flag::NotSource)
                continue;

            TablePrinter<4>::Line line = {{pkg.FullName(true), ver.VerStr(), vf.File().Archive(), std::string() + vf.File().Site()}};

            table.insert(line);
        }

    }

    table.output();
}

static void show_upgrade_info(const pkgCache::PkgIterator &p, bool show_uninstalled)
{
    if (p->SelectedState == pkgCache::State::Hold &&
        _config->FindB("APT::Show-Versions::No-Hold", false))
        return;

    if (p->CurrentVer == 0 && !show_uninstalled)
        return;

    if (_config->FindB("APT::Show-Versions::All-Versions"))
        show_all_versions(p);

    if (p->CurrentVer == 0) {
        if (show_uninstalled || _config->FindB("APT::Show-Versions::All-Versions"))
            std::cout << p.FullName(true) << " not installed\n";
        return;
    }

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
        {'R',"regex-all","apt::show-versions::regex-all",CommandLine::Boolean},
        {'n',"no-hold","apt::show-versions::no-hold",CommandLine::Boolean},
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

    pkgInitSystem(*_config, _system);
    pkgCacheFile cachefile;
    pkgCache *cache = cachefile.GetPkgCache();

    policy = cachefile.GetPolicy();

    if (cmd.FileList[0] && _config->FindB("apt::show-versions::no-hold")) {
        _error->Error("Cannot specify -n|--no-hold with a package name");
    }
    if (!cmd.FileList[0] && _config->FindB("apt::show-versions::regex-all")) {
        _error->Error("Cannot specify -R|--regex-all without a pattern");
    }

    if (cache == NULL || _error->PendingError()) {
        _error->DumpErrors();
        return 2;
    }

    if (cmd.FileList[0] == NULL) {
        for (auto p = cache->PkgBegin(); p != cache->PkgEnd(); p++)
            show_upgrade_info(p, false);
    } else {
        auto regex_all = _config->FindB("apt::show-versions::regex-all");
        for (size_t i = 0; cmd.FileList[i]; i++) {
            std::string pattern = cmd.FileList[i];
            auto pkgs = APT::PackageList::FromString(cachefile, pattern);

            _error->DumpErrors();

            for (auto pp = pkgs.begin(); pp != pkgs.end(); pp++)
                show_upgrade_info(*pp, regex_all || pkgs.getConstructor() !=
                                    APT::PackageContainerInterface::REGEX);
        }
    }

}
