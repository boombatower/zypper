/*---------------------------------------------------------------------------*\
                          ____  _ _ __ _ __  ___ _ _
                         |_ / || | '_ \ '_ \/ -_) '_|
                         /__|\_, | .__/ .__/\___|_|
                             |__/|_|  |_|
\*---------------------------------------------------------------------------*/

#ifndef ZMART_KEYRINGCALLBACKS_H
#define ZMART_KEYRINGCALLBACKS_H

#include <stdlib.h>
#include <iostream>
#include <boost/format.hpp>

#include <zypp/base/Logger.h>
#include <zypp/ZYppCallbacks.h>
#include <zypp/Pathname.h>
#include <zypp/KeyRing.h>
#include <zypp/Digest.h>

#include "Zypper.h"
#include "Table.h"

///////////////////////////////////////////////////////////////////
namespace zypp
{ /////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////
  namespace
  {
    std::ostream & dumpKeyInfo( std::ostream & str, const PublicKeyData & key, const KeyContext & context = KeyContext() )
    {
      Table t;
      t.lineStyle( none );
      if ( !context.empty() )
      {
	t << ( TableRow() << "" << _("Repository:") << context.repoInfo().asUserString() );
      }
      t << ( TableRow() << "" << _("Key Name:") << key.name() )
	<< ( TableRow() << "" << _("Key Fingerprint:") << str::gapify( key.fingerprint(), 8 ) )
	<< ( TableRow() << "" << _("Key Created:") << key.created() )
	<< ( TableRow() << "" << _("Key Expires:") << key.expiresAsString() )
	<< ( TableRow() << "" << _("Rpm Name:") << (boost::format( "gpg-pubkey-%1%-%2%" ) % key.gpgPubkeyVersion() % key.gpgPubkeyRelease()).str() );

      return str << t;
    }

    inline std::ostream & dumpKeyInfo( std::ostream & str, const PublicKey & key, const KeyContext & context = KeyContext() )
    { return dumpKeyInfo( str, key.keyData(), context ); }
  } // namespace
  ///////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////
    // KeyRingReceive
    ///////////////////////////////////////////////////////////////////

    struct KeyRingReceive : public zypp::callback::ReceiveReport<zypp::KeyRingReport>
    {
      KeyRingReceive()
          : _gopts(Zypper::instance()->globalOpts())
          {}

      ////////////////////////////////////////////////////////////////////

      virtual void infoVerify( const std::string & file_r, const PublicKeyData & keyData_r, const KeyContext & context = KeyContext() )
      {
	if ( keyData_r.expired() )
	{
	  Zypper::instance()->out().warning( boost::format(_("The gpg key signing file '%1%' has expired.")) % file_r );
	  dumpKeyInfo( (std::ostream&)ColorStream(std::cout,ColorContext::MSG_WARNING), keyData_r, context );
	}
	else if ( keyData_r.daysToLive() < 15 )
	{
	  Zypper::instance()->out().info( boost::format(
	    PL_( "The gpg key signing file '%1%' will expire in %2% day.",
		 "The gpg key signing file '%1%' will expire in %2% days.",
		 keyData_r.daysToLive() )) % file_r %  keyData_r.daysToLive() );
	  dumpKeyInfo( std::cout, keyData_r, context );
	}
	else if ( Zypper::instance()->out().verbosity() > Out::NORMAL )
	{
	  dumpKeyInfo( std::cout, keyData_r, context );
	}
      }

      ////////////////////////////////////////////////////////////////////
      virtual bool askUserToAcceptUnsignedFile(
          const std::string & file, const KeyContext & context)
      {
        if (_gopts.no_gpg_checks)
        {
          MIL << "Accepting unsigned file (" << file << ", repo: "
              << (context.empty() ? "(unknown)" : context.repoInfo().alias())
              << ")" << std::endl;

          if (context.empty())
            Zypper::instance()->out().warning(boost::str(
              boost::format(_("Accepting an unsigned file '%s'.")) % file),
              Out::HIGH);
          else
            Zypper::instance()->out().warning(boost::str(
              boost::format(_("Accepting an unsigned file '%s' from repository '%s'."))
                % file % context.repoInfo().asUserString() ),
              Out::HIGH);

          return true;
        }

        std::string question;
        if (context.empty())
          question = boost::str(boost::format(
            // TranslatorExplanation: speaking of a file
            _("File '%s' is unsigned, continue?")) % file);
        else
          question = boost::str(boost::format(
            // TranslatorExplanation: speaking of a file
            _("File '%s' from repository '%s' is unsigned, continue?"))
            % file % context.repoInfo().asUserString() );

        return read_bool_answer(PROMPT_YN_GPG_UNSIGNED_FILE_ACCEPT, question, false);
      }

      ////////////////////////////////////////////////////////////////////

      virtual bool askUserToAcceptUnknownKey(
          const std::string & file,
          const std::string & id,
          const zypp::KeyContext & context)
      {
        if (_gopts.no_gpg_checks)
        {
          MIL
            << "Accepting file signed with an unknown key ("
            << file << "," << id << ", repo: "
            << (context.empty() ? "(unknown)" : context.repoInfo().alias())
            << ")" << std::endl;

          if (context.empty())
            Zypper::instance()->out().warning(boost::str(boost::format(
                _("Accepting file '%s' signed with an unknown key '%s'."))
                % file % id));
          else
            Zypper::instance()->out().warning(boost::str(boost::format(
                _("Accepting file '%s' from repository '%s' signed with an unknown key '%s'."))
                % file % context.repoInfo().asUserString() % id));

          return true;
        }

        std::string question;
        if (context.empty())
          question = boost::str(boost::format(
            // translators: the last %s is gpg key ID
            _("File '%s' is signed with an unknown key '%s'. Continue?")) % file % id);
        else
          question = boost::str(boost::format(
            // translators: the last %s is gpg key ID
            _("File '%s' from repository '%s' is signed with an unknown key '%s'. Continue?"))
             % file % context.repoInfo().asUserString() % id);

        return read_bool_answer(PROMPT_YN_GPG_UNKNOWN_KEY_ACCEPT, question, false);
      }

      ////////////////////////////////////////////////////////////////////

      virtual KeyRingReport::KeyTrust askUserToAcceptKey(
          const PublicKey &key, const zypp::KeyContext & context)
      {
        Zypper & zypper = *Zypper::instance();
        std::ostringstream s;

	s << std::endl;
	if (_gopts.gpg_auto_import_keys)
	  s << _("Automatically importing the following key:") << std::endl;
	else if (_gopts.no_gpg_checks)
          s << _("Automatically trusting the following key:") << std::endl;
        else
          s << _("New repository or package signing key received:") << std::endl;

        // gpg key info
        dumpKeyInfo( s << std::endl, key, context )  << std::endl;

        // if --gpg-auto-import-keys or --no-gpg-checks print info and don't ask
        if (_gopts.gpg_auto_import_keys)
        {
          MIL << "Automatically importing key " << key << std::endl;
          zypper.out().info(s.str());
          return KeyRingReport::KEY_TRUST_AND_IMPORT;
        }
        else if (_gopts.no_gpg_checks)
        {
          MIL << "Automatically trusting key " << key << std::endl;
          zypper.out().info(s.str());
          return KeyRingReport::KEY_TRUST_TEMPORARILY;
        }

        // ask the user
        s << std::endl;
        // translators: this message is shown after showing description of the key
        s << _("Do you want to reject the key, trust temporarily, or trust always?");

        // only root has access to rpm db where keys are stored
        bool canimport = geteuid() == 0 || _gopts.changedRoot;

        PromptOptions popts;
        if (canimport)
          // translators: r/t/a stands for Reject/TrustTemporarily/trustAlways(import)
          // translate to whatever is appropriate for your language
          // The anserws must be separated by slash characters '/' and must
          // correspond to reject/trusttemporarily/trustalways in that order.
          // The answers should be lower case letters.
          popts.setOptions(_("r/t/a/"), 0);
        else
          // translators: the same as r/t/a, but without 'a'
          popts.setOptions(_("r/t"), 0);
        // translators: help text for the 'r' option in the 'r/t/a' prompt
        popts.setOptionHelp(0, _("Don't trust the key."));
        // translators: help text for the 't' option in the 'r/t/a' prompt
        popts.setOptionHelp(1, _("Trust the key temporarily."));
        if (canimport)
          // translators: help text for the 'a' option in the 'r/t/a' prompt
          popts.setOptionHelp(2, _("Trust the key and import it into trusted keyring."));

        if (!zypper.globalOpts().non_interactive)
          clear_keyboard_buffer();
        zypper.out().prompt(PROMPT_YN_GPG_KEY_TRUST, s.str(), popts);
        unsigned prep =
          get_prompt_reply(zypper, PROMPT_YN_GPG_KEY_TRUST, popts);
        switch (prep)
        {
        case 0:
          return KeyRingReport::KEY_DONT_TRUST;
        case 1:
          return KeyRingReport::KEY_TRUST_TEMPORARILY;
        case 2:
          return KeyRingReport::KEY_TRUST_AND_IMPORT;
        default:
          return KeyRingReport::KEY_DONT_TRUST;
        }
        return KeyRingReport::KEY_DONT_TRUST;
      }

      ////////////////////////////////////////////////////////////////////

      virtual bool askUserToAcceptVerificationFailed(
          const std::string & file,
          const PublicKey & key,
          const zypp::KeyContext & context )
      {
        if (_gopts.no_gpg_checks)
        {
          MIL << boost::format("Ignoring failed signature verification for %s")
              % file << std::endl;

          std::ostringstream msg;
          if (context.empty())
            msg << boost::format(
                _("Ignoring failed signature verification for file '%s'!")) % file;
          else
            msg << boost::format(
                _("Ignoring failed signature verification for file '%s'"
                  " from repository '%s'!")) % file
                  % context.repoInfo().asUserString();

          msg
            << std::endl
            << _("Double-check this is not caused by some malicious"
                 " changes in the file!");

          Zypper::instance()->out().warning(msg.str(), Out::QUIET);
          return true;
        }

        std::ostringstream question;
        if (context.empty())
          question << boost::format(
            _("Signature verification failed for file '%s'.")) % file;
        else
          question << boost::format(
            _("Signature verification failed for file '%s' from repository '%s'."))
              % file % context.repoInfo().asUserString();

        question
          << std::endl
          << _("Warning: This might be caused by a malicious change in the file!\n"
               "Continuing might be risky. Continue anyway?");

        return read_bool_answer(
            PROMPT_YN_GPG_CHECK_FAILED_IGNORE, question.str(), false);
      }

    private:
      const GlobalOptions & _gopts;
    };

    ///////////////////////////////////////////////////////////////////
    // DigestReceive
    ///////////////////////////////////////////////////////////////////

    struct DigestReceive : public zypp::callback::ReceiveReport<zypp::DigestReport>
    {
      DigestReceive() : _gopts(Zypper::instance()->globalOpts()) {}

      ////////////////////////////////////////////////////////////////////

      virtual bool askUserToAcceptNoDigest( const zypp::Pathname &file )
      {
	std::string question = boost::str(boost::format(
	    _("No digest for file %s.")) % file) + " " + _("Continue?");
        return read_bool_answer(PROMPT_GPG_NO_DIGEST_ACCEPT, question, _gopts.no_gpg_checks);
      }

      ////////////////////////////////////////////////////////////////////

      virtual bool askUserToAccepUnknownDigest( const Pathname &file, const std::string &name )
      {
        std::string question = boost::str(boost::format(
            _("Unknown digest %s for file %s.")) %name % file) + " " +
            _("Continue?");
        return read_bool_answer(PROMPT_GPG_UNKNOWN_DIGEST_ACCEPT, question, _gopts.no_gpg_checks);
      }

      ////////////////////////////////////////////////////////////////////

      virtual bool askUserToAcceptWrongDigest( const Pathname &file, const std::string &requested, const std::string &found )
      {
	Zypper & zypper = *Zypper::instance();
	std::string unblock( found.substr( 0, 4 ) );

	zypper.out().gap();
	// translators: !!! BOOST STYLE PLACEHOLDERS ( %N% - reorder and multiple occurance is OK )
	// translators: %1%      - a file name
	// translators: %2%      - full path name
	// translators: %3%, %4% - checksum strings (>60 chars), please keep them alligned
	zypper.out().warning( boost::formatNAC(_(
		"Digest verification failed for file '%1%'\n"
		"[%2%]\n"
		"\n"
		"  expected %3%\n"
		"  but got  %4%\n" ) )
		% file.basename()
		% file
		% requested
		% found
	);

	zypper.out().info( ColorString( ColorContext::MSG_WARNING, _(
		"Accepting packages with wrong checksums can lead to a corrupted system "
		"and in extreme cases even to a system compromise." ) ).str()
	);
	zypper.out().gap();

	// translators: !!! BOOST STYLE PLACEHOLDERS ( %N% - reorder and multiple occurance is OK )
	// translators: %1%      - abbreviated checksum (4 chars)
	zypper.out().info( boost::formatNAC(_(
		"However if you made certain that the file with checksum '%1%..' is secure, correct\n"
		"and should be used within this operation, enter the first 4 characters of the checksum\n"
		"to unblock using this file on your own risk. Empty input will discard the file.\n" ) )
		% unblock
	);

	// translators: A prompt option
	PromptOptions popts( unblock+"/"+_("discard"), 1 );
	// translators: A prompt option help text
	popts.setOptionHelp( 0, _("Unblock using this file on your own risk.") );
	// translators: A prompt option help text
	popts.setOptionHelp( 1, _("Discard the file.") );
	popts.setShownCount( 1 );
	if ( !zypper.globalOpts().non_interactive )
	  clear_keyboard_buffer();
	// translators: A prompt text
	zypper.out().prompt( PROMPT_GPG_WRONG_DIGEST_ACCEPT, _("Unblock or discard?"), popts );
	int reply = get_prompt_reply( zypper, PROMPT_GPG_WRONG_DIGEST_ACCEPT, popts );
	return( reply == 0 );
      }

    private:
      const GlobalOptions & _gopts;
    };

   ///////////////////////////////////////////////////////////////////
}; // namespace zypp
///////////////////////////////////////////////////////////////////

class KeyRingCallbacks {

  private:
    zypp::KeyRingReceive _keyRingReport;

  public:
    KeyRingCallbacks()
    {
      _keyRingReport.connect();
    }

    ~KeyRingCallbacks()
    {
      _keyRingReport.disconnect();
    }

};

class DigestCallbacks {

  private:
    zypp::DigestReceive _digestReport;

  public:
    DigestCallbacks()
    {
      _digestReport.connect();
    }

    ~DigestCallbacks()
    {
      _digestReport.disconnect();
    }

};


#endif // ZMART_KEYRINGCALLBACKS_H
// Local Variables:
// c-basic-offset: 2
// End:
