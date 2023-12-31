/*
 *	PROGRAM:		Firebird authentication
 *	MODULE:			AuthDbg.cpp
 *	DESCRIPTION:	Test module for various auth types
 *					NOT FOR PRODUCTION USE !
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2010 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../auth/AuthDbg.h"
#include "ibase.h"
#include "../common/StatusHolder.h"

#ifdef AUTH_DEBUG

//#define AUTH_VERBOSE

// register plugin
static Firebird::SimpleFactory<Auth::DebugClient> clientFactory;
static Firebird::SimpleFactory<Auth::DebugServer> serverFactory;

extern "C" FB_DLL_EXPORT void FB_PLUGIN_ENTRY_POINT(Firebird::IMaster* master)
{
	Firebird::CachedMasterInterface::set(master);

	const char* name = "Auth_Debug";

	Firebird::PluginManagerInterfacePtr iPlugin;

	iPlugin->registerPluginFactory(Firebird::IPluginManager::TYPE_AUTH_CLIENT, name, &clientFactory);
	iPlugin->registerPluginFactory(Firebird::IPluginManager::TYPE_AUTH_SERVER, name, &serverFactory);
}


namespace Auth {

DebugServer::DebugServer(Firebird::IPluginConfig* pConf)
	: str(getPool())
{
	Firebird::LocalStatus ls;
	Firebird::CheckStatusWrapper s(&ls);
	config.assignRefNoIncr(pConf->getDefaultConfig(&s));
	check(&s);
}

int DebugServer::authenticate(Firebird::CheckStatusWrapper* status, Firebird::IServerBlock* sb,
	Firebird::IWriter* writerInterface)
{
	try
	{
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugServer::authenticate\n");
#endif
		unsigned int length;
		const unsigned char* val = sb->getData(&length);
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugServer::authenticate: get()=%.*s\n", length, val);
#endif

		if (str.isEmpty())
		{
			str.assign(val, length);

			str += '_';
#ifdef AUTH_VERBOSE
			fprintf(stderr, "DebugServer::authenticate1: %s\n", str.c_str());
#endif
			sb->putData(status, str.length(), str.c_str());
			if (status->getState() & Firebird::IStatus::STATE_ERRORS)
			{
				return AUTH_FAILED;
			}

			return AUTH_MORE_DATA;
		}

		str.assign(val, length);
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugServer::authenticate2: %s\n", str.c_str());
#endif
		Firebird::LocalStatus ls;
		Firebird::CheckStatusWrapper s(&ls);
		writerInterface->add(&s, str.c_str());
		check(&s);
		str.erase();

		Firebird::RefPtr<Firebird::IConfigEntry> group(Firebird::REF_NO_INCR, config->find(&s, "GROUP"));
		check(&s);
		if (group)
		{
			writerInterface->add(&s, group->getValue());
			check(&s);
			writerInterface->setType(&s, "GROUP");
			check(&s);
		}

		Firebird::RefPtr<Firebird::IConfigEntry> multi(Firebird::REF_NO_INCR, config->find(&s, "MULTIGROUPS"));
		check(&s);
		if (multi)
		{
			const int limit = multi->getIntValue();
			// list groups using writerInterface
			const char* grName = "FillWithBigData";
			for (int n = 0; n < limit; ++n)
			{
				writerInterface->add(status, grName);
				if (status->getState() & Firebird::IStatus::STATE_ERRORS)
					return AUTH_FAILED;
				writerInterface->setType(status, "Group");
				if (status->getState() & Firebird::IStatus::STATE_ERRORS)
					return AUTH_FAILED;
			}
		}

		return AUTH_SUCCESS;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}

	return AUTH_FAILED;
}

void DebugServer::setDbCryptCallback(Firebird::CheckStatusWrapper*, Firebird::ICryptKeyCallback*)
{ /* ignore it */ }

DebugClient::DebugClient(Firebird::IPluginConfig*)
	: str(getPool())
{ }

int DebugClient::authenticate(Firebird::CheckStatusWrapper* status, Firebird::IClientBlock* cb)
{
	try
	{
		if (cb->getLogin())
		{
			// user specified login - we should not continue with trusted-like auth
			return AUTH_CONTINUE;
		}

		if (str != "HAND")
		{
			str = "HAND";
		}
		else
		{
			unsigned int length;
			const unsigned char* v = cb->getData(&length);
			str.assign(v, length);

#ifdef AUTH_VERBOSE
			fprintf(stderr, "DebugClient::authenticate2: from srv=%s\n", str.c_str());
#endif

			const char* env = getenv("ISC_DEBUG_AUTH");
			if (env && env[0] && str.length() > 0 && str[str.length() - 1] == '_')
				str = env;
			else
				str += "SHAKE";
		}

#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugClient::authenticate: sending %s\n", str.c_str());
#endif
		cb->putData(status, str.length(), str.c_str());
		if (status->getState() & Firebird::IStatus::STATE_ERRORS)
		{
			return AUTH_FAILED;
		}

#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugClient::authenticate: data filled\n");
#endif
		return AUTH_SUCCESS;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}

	return AUTH_FAILED;
}

} // namespace Auth

#endif // AUTH_DEBUG
