#include <curl/curl.h>
#include "sdk/amxxmodule.h"
#include "amx_curl_controller_class.h"
#include "asio_poller.h"

extern AMX_NATIVE_INFO g_amx_curl_natives[];

// KTP: Frame callback for async cURL processing
// This replaces Metamod's StartFrame callback
void CurlFrameCallback()
{
    AmxCurlController::Instance().get_asio_poller().Poll();
}

// amxmodx

void OnAmxxAttach()
{
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if(res != CURLE_OK)
    {
        MF_PrintSrvConsole("[CURL] Cannot init curl: %s\n", curl_easy_strerror(res));
        return;
    }

    MF_AddNatives(g_amx_curl_natives);

    // KTP: Register frame callback for async processing (KTPAMXX only)
    if (MF_RegModuleFrameFunc)
    {
        MF_RegModuleFrameFunc(CurlFrameCallback);
        MF_PrintSrvConsole("[CURL] Module loaded (extension mode, using frame callbacks)\n");
    }
    else
    {
        MF_PrintSrvConsole("[CURL] Module loaded\n");
    }
}

void OnAmxxDetach()
{
    // KTP: Unregister frame callback
    if (MF_UnregModuleFrameFunc)
        MF_UnregModuleFrameFunc(CurlFrameCallback);

    // Wait for all transfers to complete and clean up
    // (Replaces Metamod's ServerDeactivate callback)
    AmxCurlManager& manager = AmxCurlController::Instance().get_curl_manager();

    while(!manager.IsAllTransfersCompleted())
        AmxCurlController::Instance().get_asio_poller().Poll();
    manager.RemoveAllTasks();
}
