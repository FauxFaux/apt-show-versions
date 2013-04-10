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
#include <algorithm>
#include <stdexcept>

static pkgSourceList *list;
static pkgPolicy *policy;


/**
 * \brief The official suites
 *
 * The first element is obviously not an official suite, it is just added to
 * make code writing easier.
 */
static const char *official_suites[] = {
    "",
    "oldstable",
    "stable",
    "proposed-updates",
    "stable-updates",
    "testing",
    "testing-proposed-updates",
    "testing-updates",
    "unstable",
    "experimental",
    NULL
};

/**
 * \brief Find a distribution in the sources.list file
 *
 * This keeps a cache of previous call results, as a lookup currently involves
 * several stat() syscalls when calling FindInCache().
 */
static const std::string& find_distribution_name(pkgCache::PkgFileIterator file)
{
    static std::map<unsigned int, std::string> map;

    try {
        return map.at(file->ID);
    } catch (const std::out_of_range& e) {
    }

    for (auto i = list->begin(); i != list->end(); ++i) {
        vector<pkgIndexFile *> *indexes = (*i)->GetIndexFiles();
        for (auto filep = indexes->begin(); filep != indexes->end(); ++filep) {
            if ((*filep)->FindInCache(*file.Cache()) == file) {
                std::string distro = (**i).GetDist();
                /* For stable/updates and similar, we want to display stable */
                size_t subdistro = distro.find_first_of('/');
                if (subdistro != std::string::npos)
                    distro.erase(subdistro);

                if ((file->Archive && distro == file.Archive()) ||
                    (file->Codename && distro == file.Codename()))
                    return map[file->ID] = distro;
            }
      }
   }

    if (file->Archive)
        return map[file->ID] = std::string(file.Archive());
    if (file->Codename)
        return map[file->ID] = std::string(file.Codename());
    return map[file->ID] = std::string();
}

/**
 * \brief Generate a name to display for a given package and candidate
 *
 * This returns the name of the package (possibly qualified with architecture)
 * and if available, the name of the distribution it comes from. If the package
 * exists in multiple distributions, the distribution with the highest priority
 * is chosen.
 *
 * \param p The package to display
 * \param c The candidate to take the distribution info from
 */
static std::string my_name(pkgCache::PkgIterator p, pkgCache::VerIterator c)
{
    std::string name = p.FullName(true);
    std::string my;
    int prio = 0;

    for (auto vf = c.FileList(); c.IsGood(); c++) {
        auto this_prio = policy->GetPriority(vf.File());
        if (vf.File()->Flags & pkgCache::Flag::NotSource)
            continue;
        else if (!my.empty() && prio >= this_prio)
            continue;

        std::string distro = find_distribution_name(vf.File());
        if (!distro.empty()) {
            my = name + "/" + distro;
            prio = this_prio;
        }
    }
    return my.empty() ? name : my;
}

/**
 * \brief Returns a null output stream if APT::Show-Versions::Brief is false
 *
 * If APT::Show-Versions::Brief is true, this prints a newline characters and
 * returns a stream to /dev/null. Otherwise, it returns the stream it received
 * as an argument.
 */
static std::ostream& print_only(std::ostream& in)
{
    static std::ofstream nullstream("/dev/null");

    if (!_config->FindB("APT::Show-Versions::Brief"))
        return in;

    in << "\n";
    return nullstream;
}

/**
 * \brief Helper class to display a table
 *
 * This prints a table with aligned output.
 */
template<size_t N> struct TablePrinter {
    typedef std::array<std::string, N>  Columns;
    struct Line {
        int tag;
        std::string s;
        Columns l;
    };
    std::vector<Line> lines;
    size_t max[N];

    TablePrinter() {
        for (size_t i = 0; i < N; i++)
            max[i] = 0;
    }

    void insert(Columns &line) {
        for (size_t i = 0; i < N; i++)
            max[i] = line[i].size() > max[i] ? line[i].size() : max[i];

        Line l = {1, "", line};

        lines.push_back(l);
    }

    void insert(const std::string& line) {
        Line l = {0, line, Columns()};
        lines.push_back(l);
    }

    void output() {
        std::cout.setf(std::ios::left);
        for (auto line = lines.begin(); line != lines.end(); line++) {
            if (line->tag == 0) {
                std::cout << line->s;
            } else {
                for (size_t i = 0; i < N; i++)
                    std::cout << std::setw(max[i] + (i < N - 1)) << line->l[i];
            }

            std::cout << "\n";
        }
    }
};

/**
 * \brief Returns a dpkg Status line, for displaying purposes
 */
static void describe_state(const pkgCache::PkgIterator &pkg)
{
    static const char *selections[] = {"unknown", "install", "hold",
                                       "deinstall", "purge"};
    static const char *installs[] = {"ok", "reinstreq", "hold",
                                     "hold-reinstreq"};
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

/**
 * \brief Possible upgrade states
 */
enum upgrade_state {
    /** The package in question is not installed */
    UPGRADE_NOT_INSTALLED,
    /** The package is not available anymore */
    UPGRADE_NOT_AVAIL,
    /** The package is up-to-date */
    UPGRADE_UPTODATE,
    /** The installed version is not available anymore, but a downgrade */
    UPGRADE_DOWNGRADE,
    /** An upgrade can be performed automatically */
    UPGRADE_AUTOMATIC,
    /** A manual upgrade can be performed */
    UPGRADE_MANUAL,
};

/**
 * \brief Determine the upgrade state of a package
 */
static upgrade_state determine_upgradeability(const pkgCache::PkgIterator &p)
{
    auto current = p.CurrentVer();
    auto candidate = policy->GetCandidateVer(p);
    auto newer = p.VersionList();

    if (current.end())
        return UPGRADE_NOT_INSTALLED;
    else if (p.VersionList()->NextVer == 0 && current.FileList()->NextFile == 0)
        return UPGRADE_NOT_AVAIL;
    else if (candidate->ID != current->ID)
        return UPGRADE_AUTOMATIC;
    else if (current.FileList()->NextFile != 0)
        return UPGRADE_UPTODATE;
    else if (newer.IsGood() && newer->ID != current->ID)
        return UPGRADE_MANUAL;
    else if (current->NextVer != 0)
        return UPGRADE_DOWNGRADE;

    __builtin_trap();
}

/**
 * \brief Enable ordering for packages.
 *
 * We use that operator overload to have our APT::PackageSet instance
 * sorted.
 */
static bool operator <(const pkgCache::PkgIterator &a,
                       const pkgCache::PkgIterator &b)
{
    int value = strcmp(a.Name(), b.Name());

    if (unlikely(value == 0))
        value = strcmp(a.Arch(), b.Arch());

    return value < 0;
}

/**
 * \brief Check whether a given suite is present in the cache
 */
bool suite_is_in_cache(pkgCache *cache, const char *name) {
    for (auto f = cache->FileBegin(); f != cache->FileEnd(); f++)
            if (f->Archive && strcmp(f.Archive(), name) == 0)
                return true;

    return false;
}
/**
 * \brief Check whether a given suite is an official one
 */
bool suite_is_official(pkgCache::PkgFileIterator file) {
    for (auto s = official_suites; *s; s++)
            if (**s && file->Archive && strcmp(*s, file.Archive()) == 0)
                return true;

    return false;
}

/**
 * \brief Implementation of parts of the --allversions option
 */
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

    for (auto release = official_suites; *release; release++) {
        if (release != official_suites && !suite_is_in_cache(pkg.Cache(), *release))
            continue;

        bool found = false;
        for (auto ver = pkg.VersionList(); ver.IsGood(); ver++) {
            for (auto vf = ver.FileList(); vf.IsGood(); vf++) {
                if (vf.File()->Flags & pkgCache::Flag::NotSource)
                    continue;

                if (release == official_suites) {
                    if (suite_is_official(vf.File()))
                        continue;
                } else if (strcmp(vf.File().Archive(), *release) != 0) {
                    continue;
                }

                found = true;

                TablePrinter<4>::Columns line = {{pkg.FullName(true), ver.VerStr(), find_distribution_name(vf.File()), std::string(vf.File().Site())}};

                table.insert(line);
            }
        }

        if (!found && release != official_suites)
            table.insert(std::string("No ") + *release + " version");
    }

    table.output();
}

/**
 * \brief Shows information about upgradeability of a single package
 */
static void show_upgrade_info(const pkgCache::PkgIterator &p, bool show_uninstalled)
{
    if ((p->CurrentVer == 0 && !show_uninstalled))
        return;
    if (p->SelectedState == pkgCache::State::Hold &&
        _config->FindB("APT::Show-Versions::No-Hold", false))
        return;

    const upgrade_state state = determine_upgradeability(p);

    if (state < UPGRADE_AUTOMATIC && _config->FindB("APT::Show-Versions::Upgrades-Only"))
        return;

    if (_config->FindB("APT::Show-Versions::All-Versions"))
        show_all_versions(p);

    auto current = p.CurrentVer();
    auto candidate = policy->GetCandidateVer(p);
    auto newer = p.VersionList();

    if (state == UPGRADE_NOT_INSTALLED) {
        if (show_uninstalled || _config->FindB("APT::Show-Versions::All-Versions"))
            std::cout << p.FullName(true) << " not installed\n";
    } else if (state == UPGRADE_AUTOMATIC) {
        print_only(std::cout << my_name(p, candidate)) << " upgradeable from " << current.VerStr() << " to " << candidate.VerStr() << "\n";
    } else if (state == UPGRADE_MANUAL) {
        print_only(std::cout << my_name(p, newer)) << " *manually* upgradeable from " << current.VerStr() << " to " << newer.VerStr() << "\n";
    } else if (_config->FindB("APT::Show-Versions::Upgrades-Only")) {
    } else if (state == UPGRADE_NOT_AVAIL) {
        std::cout << p.FullName(true) << " " << current.VerStr() << " installed: No available version in archive\n";
    } else if (state == UPGRADE_UPTODATE) {
        print_only(std::cout << my_name(p, candidate)) << " uptodate " << current.VerStr() << "\n";
    } else if (state == UPGRADE_DOWNGRADE) {
        print_only(std::cout << my_name(p, candidate)) << " " << current.VerStr() << " newer than version in archive\n";
    }
}

/**
 * \brief Shows help output
 */
static void show_help()
{
    std::cout << "apt-show-versions using APT " << pkgVersion << "\n\n";
    std::cout << "Usage:\n";
    std::cout << " apt-show-versions            shows available versions of installed packages\n\n";
    std::cout << "Options:\n";
    std::cout << " -c=?                         configuration file\n";
    std::cout << " -o=?                         option\n";
    std::cout << " -R,--regex-all               regular expressions apply to uninstalled packages\n";
    std::cout << " -u,--upgradeable             show only upgradeable packages\n";
    std::cout << " -a,--allversions             show all versions\n";
    std::cout << " -b,--brief                   show package names only\n";
    std::cout << " -n,--no-hold                 do not show hold packages\n";
    std::cout << " -h,--help                    show help\n";
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
        {'i',"initialize","apt::show-versions::initialize-cache",CommandLine::Boolean},
        {'v',"verbose","apt::show-versions::dummy-option",CommandLine::Boolean},
        {'a',"allversions","apt::show-versions::all-versions",CommandLine::Boolean},
        {'R',"regex-all","apt::show-versions::regex-all",CommandLine::Boolean},
        {'n',"no-hold","apt::show-versions::no-hold",CommandLine::Boolean},
        {'p',"package","apt::show-versions::package",CommandLine::HasArg},
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

    list = cachefile.GetSourceList();
    policy = cachefile.GetPolicy();

    if (cmd.FileList[0] && _config->FindB("apt::show-versions::no-hold")) {
        _error->Error("Cannot specify -n|--no-hold with a package name");
    }
    if (!cmd.FileList[0] && _config->FindB("apt::show-versions::regex-all")) {
        _error->Error("Cannot specify -R|--regex-all without a pattern");
    }

    /* Hack backward compatibility for -p back in */
    if (!_config->Find("apt::show-versions::package").empty()) {
        if (cmd.FileList[0])
            _error->Error("Cannot specify -p|--package and more package names");

        cmd.FileList = new const char *[2];
        cmd.FileList[0] = strdup(_config->Find("apt::show-versions::package").c_str());
        cmd.FileList[1] = NULL;
    }

    if (_config->FindB("apt::show-versions::initialize-cache")) {
        _error->Warning("Use apt-cache gencaches instead of %s -i", argv[0]);
        if (!_error->PendingError()) {
            _error->DumpErrors();
            return 0;
        }
    }

    if (cache == NULL || _error->PendingError()) {
        _error->DumpErrors();
        return 1;
    }

    if (cmd.FileList[0] == NULL) {
        std::vector<pkgCache::Group*> groups(cache->HeaderP->GroupCount);
        for (auto p = cache->GrpBegin(); p != cache->GrpEnd(); p++)
            groups[p->ID] = p;

        std::sort(groups.begin(), groups.end(),
                  [cache](pkgCache::Group *a, pkgCache::Group *b) {
                    return strcmp(cache->StrP + a->Name,
                                  cache->StrP + b->Name) < 0;
        });

        for (size_t i = 0; i < cache->HeaderP->GroupCount; i++) {
            pkgCache::GrpIterator grp(*cache, groups[i]);
            for (auto p = grp.PackageList(); !p.end(); p = grp.NextPkg(p))
                show_upgrade_info(p, false);
        }
    } else {
        auto regex_all = _config->FindB("apt::show-versions::regex-all");
        for (size_t i = 0; cmd.FileList[i]; i++) {
            std::string pattern = cmd.FileList[i];
            auto pkgs = APT::PackageSet::FromString(cachefile, pattern);

            _error->DumpErrors();

            for (auto pp = pkgs.begin(); pp != pkgs.end(); pp++)
                show_upgrade_info(*pp, regex_all || pkgs.getConstructor() ==
                                  APT::PackageContainerInterface::UNKNOWN);

            /* If only a single package name is given, and -u is specified,
             * we should exit with code 2.
             */
            if (pkgs.getConstructor() == APT::PackageContainerInterface::UNKNOWN
                && cmd.FileList[1] == NULL
                && _config->FindB("apt::show-versions::upgrades-only")
                && pattern.find('*') == std::string::npos
                && (pkgs.begin() == pkgs.end() ||
                    determine_upgradeability(*pkgs.begin()) < UPGRADE_AUTOMATIC))
                return 2;
        }
    }

}
