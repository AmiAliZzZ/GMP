
#include "StdInc.h"
#include <ServerIdentityProvider.h>

#include <json.hpp>
#include "ServerLicensingComponent.h"

#include <ServerInstanceBase.h>
#include <ServerInstanceBaseRef.h>
#include <ResourceManager.h>
#include <HttpClient.h>

#include <CoreConsole.h>

#include <GameServer.h>
#include <TcpListenManager.h>
#include <Error.h>

#include "boost/filesystem.hpp"
#include <boost/algorithm/string.hpp>

#include <regex>
#include <map>

static bool licenseChecked = false;
static HttpClient* httpClient;

static InitFunction httpinitFunction([]()
{
	httpClient = new HttpClient();

	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* instance)
	{
		if (!licenseChecked)
		{
			console::Printf("Server Auth", "Checking license...\n");
			auto var = instance->GetComponent<console::Context>()->GetVariableManager()->FindEntryRaw("sv_licenseKey");
			if (var && !var->GetValue().empty())
			{
				auto jsonData = nlohmann::json::object({ { "license", var->GetValue() } });
				HttpRequestOptions opts;
				opts.ipv4 = true;
				opts.headers = std::map<std::string, std::string>{
					{ "Content-Type", "application/json; charset=utf-8" }
				};
				opts.addErrorBody = true;
				httpClient->DoPostRequest(fmt::sprintf(GMP_LICENSING_EP "server/register.php?work=register"), jsonData.dump(), opts, [instance](bool success, const char* data, size_t length)
				{
					if (!success)
					{
						FatalError("A connection with the GMP licensing service could not be established!");
						return;
					}

					if (!data || length == 0)
					{
						FatalError("GMP licensing service returned an empty response.");
						return;
					}

					try
					{
						auto jsonData = nlohmann::json::parse(std::string(data, length));
						if (jsonData.value("status", 0) == 1)
						{
							console::Printf("Server Auth", "GMP server license authentication succeeded!\n");
							auto consoleCtx = instance->GetComponent<console::Context>();
							{
								se::ScopedPrincipal principalScope(se::Principal{ "system.console" });
								consoleCtx->ExecuteSingleCommandDirect(ProgramArguments{ "sets", "sv_sessionId", jsonData.value("session_id", "0") });
								consoleCtx->ExecuteSingleCommandDirect(ProgramArguments{ "set", "sv_secret", jsonData.value("secret", "0") });
							}
							auto gameServer = instance->GetComponent<fx::GameServer>();
							gameServer->ForceHeartbeat();
							console::Printf("Server Auth", "GMP Session Id : %s\n", jsonData.value("session_id", "0"));
							licenseChecked = true;
						}
						else
						{
							const auto errorMessage = jsonData.value("message", std::string("GMP license authentication failed"));
							FatalError("%s", errorMessage.c_str());
						}
					}
					catch (const std::exception& e)
					{
						FatalError("GMP licensing service returned an invalid response: %s", e.what());
					}
				});
			}
			else
			{
				FatalError("Please set sv_licenseKey in server.cfg!");
			}
		}
	},
	1);
});