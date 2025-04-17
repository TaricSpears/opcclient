#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <string>

bool connection(UA_Client* client, const std::string& endpoint, const std::string& username, const std::string& password) {
    // Creazione del client
    client = UA_Client_new();
    if (!client) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Errore nella creazione del client.");
        return false;
    }

    // Configurazione del client
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    // Impostazione delle credenziali
    UA_UserNameIdentityToken* userIdentity = UA_UserNameIdentityToken_new();
    UA_String_init(&userIdentity->userName);
    UA_String_init(&userIdentity->password);
    userIdentity->userName = UA_STRING_ALLOC(username.c_str());
    userIdentity->password = UA_STRING_ALLOC(password.c_str());

    UA_StatusCode retval = UA_Client_connectUsername(client, endpoint.c_str(), username.c_str(), password.c_str());
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Connessione fallita con codice: %s", UA_StatusCode_name(retval));
        UA_Client_delete(client);
        return false;
    }

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Connessione stabilita con successo.");
    return true;
}
