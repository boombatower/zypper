/*---------------------------------------------------------------------------*\
                          ____  _ _ __ _ __  ___ _ _
                         |_ / || | '_ \ '_ \/ -_) '_|
                         /__|\_, | .__/ .__/\___|_|
                             |__/|_|  |_|
\*---------------------------------------------------------------------------*/


/** \file SolverRequester.h
 * 
 */

#ifndef SOLVERREQUESTER_H_
#define SOLVERREQUESTER_H_

#include <string>

#include "zypp/PoolItem.h"

#include "Command.h"
#include "PackageArgs.h"
#include "utils/misc.h" // for ResKindSet; might make sense to move this elsewhere


class Out;

/**
 * Issue various requests to the dependency resolver, based on given
 * arguments (\ref PackageArgs) and options (\ref SolverRequester::Options).
 */
class SolverRequester
{
public:
  struct Options
  {
    Options()
      : force(false)
      , force_by_cap(false)
      , force_by_name(false)
      , best_effort(false)
      , skip_interactive(false)
    {}

    void setForceByName(bool value = true);
    void setForceByCap (bool value = true);

    /**
     * If true, the requester will force the operation in some defined cases,
     * even if the operation would otherwise not be allowed.
     *
     * \todo define the cases
     */
    bool force;

    /**
     * Force package selection by capabilities they provide.
     * Do not try to select packages by name first.
     */
    bool force_by_cap;

    /**
     * Force package selection by name.
     * Do not fall back to selection by capability if give package is not found
     * by name.
     */
    bool force_by_name;

    /**
     * Whether to request updates via Selectable::updateCandidate() or
     * via addRequires(higher-than-installed)
     */
    bool best_effort;

    /**
     * Whether to skip installs/updates that need user interaction.
     */
    bool skip_interactive;

    /** Aliases of the repos from which the packages should be installed */
    std::list<std::string> from_repos;
  };


  /**
   * A piece of feedback from the SolverRequester.
   */
  class Feedback
  {
  public:
    enum Id
    {
      /**
       * Given combination of arguments and options makes no sense or the
       * functionality is not defined and implemented
       */
      INVALID_REQUEST,

      /** The search for the string in object names failed, but will try caps */
      NOT_FOUND_NAME_TRYING_CAPS,
      NOT_FOUND_NAME,
      NOT_FOUND_CAP,

      /** Removal or update was requested, but there's no installed item. */
      NOT_INSTALLED,

      /** Removal by capability requested, but no provider is installed. */
      NO_INSTALLED_PROVIDER,

      /** Selected object is already installed. */
      ALREADY_INSTALLED,
      NO_UPD_CANDIDATE,
      UPD_CANDIDATE_CHANGES_VENDOR,
      UPD_CANDIDATE_HAS_LOWER_PRIO,
      UPD_CANDIDATE_IS_LOCKED,

      /**
       * Selected object is not the highest available, because of user
       * restrictions like repo(s), version, architecture.
       */
      UPD_CANDIDATE_USER_RESTRICTED,
      INSTALLED_LOCKED,

      /**
       * Selected object is older that the installed. Won't allow downgrade
       * unless --force is used.
       */
      SELECTED_IS_OLDER,

      // ********* patch/pattern/product specialties **************************

      /**
       * !patch.status().isRelevant()
       */
      PATCH_NOT_NEEDED,

      /**
       * Skipping a patch because it is marked as interactive or has
       * license to confirm and --skip-interactive is requested
       * (also --non-interactive, since it implies --skip-interactive)
       */
      PATCH_INTERACTIVE_SKIPPED,

      // ********** zypp requests *********************************************

      SET_TO_INSTALL,
      FORCED_INSTALL,
      SET_TO_REMOVE,
      ADDED_REQUIREMENT,
      ADDED_CONFLICT
    };

    Feedback(
        const Id id,
        const zypp::Capability & reqcap,
        const std::string & reqrepo = std::string(),
        const zypp::PoolItem & selected = zypp::PoolItem(),
        const zypp::PoolItem & installed = zypp::PoolItem())
      : _id(id)
      , _reqcap(reqcap)
      , _reqrepo(reqrepo)
      , _objsel(selected)
      , _objinst(installed)
    {}

    Id id() const
    { return _id; }

    const zypp::PoolItem selectedObj() const
    { return _objsel; }

    std::string asUserString(const SolverRequester::Options & opts) const;
    void print(Out & out, const SolverRequester::Options & opts) const;

  private:
    Id _id;

    zypp::Capability _reqcap;
    std::string _reqrepo;

    /** The selected object */
    zypp::PoolItem _objsel;
    /** The installed object */
    zypp::PoolItem _objinst;
  };

public:
  SolverRequester(const Options & opts = Options())
    : _opts(opts), _command(ZypperCommand::NONE)
  {}

public:
  /** Request installation of specified objects. */
  void install(const PackageArgs & args);

  /**
   * Request removal of specified objects.
   * \note Passed \a args must be constructed with
   *       PackageArgs::Options::do_by_default = false.
   */
  void remove(const PackageArgs & args);

  /**
   * Variant of remove(const PackageArgs&) to avoid implicit conversion
   * from vector<string> to PackageArgs.
   */
  void remove(
      const std::vector<std::string> & rawargs,
      const zypp::ResKind & kind = zypp::ResKind::package)
  {
    PackageArgs::Options opts;
    opts.do_by_default = false;
    remove(PackageArgs(rawargs, kind, opts));
  }

  /** Request update of specified objects. */
  void update(const PackageArgs & args);

  /** Request update of all satisfied patterns.
   * TODO define this */
  void updatePatterns();

  /**
   * Request installation of all needed patches.
   * If there are any needed patches flagged "affects package management", only
   * these get selected (need to call this again once these are installed).
   */
  void updatePatches();

  bool hasFeedback(const Feedback::Id id) const;
  void printFeedback(Out & out) const
  { for_(fb, _feedback.begin(), _feedback.end()) fb->print(out, _opts); }

  const std::set<zypp::PoolItem> & toInstall() const { return _toinst; }
  const std::set<zypp::PoolItem> & toRemove() const { return _toremove; }
  const std::set<zypp::Capability> & requires() const { return _requires; }
  const std::set<zypp::Capability> & conflicts() const { return _conflicts; }

private:
  void installRemove(const PackageArgs & args);

  /**
   * Requests installation or update to the best of objects available in repos
   * according to specified arguments and options.
   *
   * If at least one provider of given \a cap is already installed,
   * this method checks for available update candidates and tries to select
   * the best for installation (thus update). Reports any problems or interesting
   * info back to user.
   *
   * \param cap       Capability object carrying specification of requested
   *                  object (name/edtion/arch). The requester will request
   *                  the best of matching objects.
   * \param repoalias Restrict the candidate lookup to specified repo
   *                  (in addition to those given in Options::from_repos).
   *
   * \note Glob can be used in the capability name for matching by name.
   * \see Options
   */
  void install(const PackageSpec & pkg);

  /**
   * Request removal of all packages matching given \a cap by name/edition/arch,
   * or providing the capability.
   *
   * Glob can be used in the capability name for matching by name.
   * If \a repoalias is given, restrict the candidate lookup to specified repo.
   *
   * \see Options
   */
  void remove(const PackageSpec & pkg);

  /**
   * Update to specified \a candidate and report if there's any better
   * candidate when restrictions like vendor change, repository priority,
   * requested repositories, arch, edition and similar are not applied.
   * Use this method only if you have already chosen the candidate. The \a cap
   * and \a repoalias only carry information about the original request so that
   * proper feedback can be provided.
   *
   * \param cap
   * \param repoalias
   * \param candidate
   */
  void updateTo(
      const zypp::Capability & cap,
      const std::string & repoalias,
      const zypp::PoolItem & selected);

  /**
   * Set the best version of the patch for installation.
   *
   * \param cap            Capability describing the patch (can have version).
   * \param ignore_pkgmgmt Whether to ignore the "affects package management"
   *                       flag. If false and the patch is flagged as such, this
   *                       method will do nothing and return false.
   * \return True if the patch was set for installation, false otherwise.
   */
  bool installPatch(
      const zypp::Capability & cap,
      const std::string & repoalias,
      const zypp::PoolItem & selected,
      bool ignore_pkgmgmt = true);

  // TODO provide also public versions of these, taking optional Options and
  // reporting errors via an output argument.

  void setToInstall(const zypp::PoolItem & pi);
  void setToRemove(const zypp::PoolItem & pi);
  void addRequirement(const zypp::Capability & cap);
  void addConflict(const zypp::Capability & cap);

  void addFeedback(
      const Feedback::Id id,
      const zypp::Capability & reqcap,
      const std::string & reqrepo = std::string(),
      const zypp::PoolItem & selected = zypp::PoolItem(),
      const zypp::PoolItem & installed = zypp::PoolItem())
  {
    _feedback.push_back(Feedback(id, reqcap, reqrepo, selected, installed));
  }

private:
  /** Various options to be applied to each requested package */
  Options _opts;

  /** Requester command being executed.
   * \note This may be different from command given on command line.
   */
  ZypperCommand _command;

  /** Various feedback from the requester. */
  std::vector<Feedback> _feedback;

  std::set<zypp::PoolItem> _toinst;
  std::set<zypp::PoolItem> _toremove;
  std::set<zypp::Capability> _requires;
  std::set<zypp::Capability> _conflicts;
};

#endif /* SOLVERREQUESTER_H_ */