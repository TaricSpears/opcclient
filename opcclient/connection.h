#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <string>
#include <stdexcept>

inline bool connection(UA_Client* client, const std::string& endpoint, const std::string& username, const std::string& password) {

        UA_ClientConfig_setDefault(UA_Client_getConfig(client));
        UA_StatusCode retval = UA_Client_connectUsername(client, endpoint.c_str(), username.c_str(), password.c_str());
    return retval;
}
