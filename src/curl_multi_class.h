#ifndef _CURL_MULTI_CLASS_H_
#define _CURL_MULTI_CLASS_H_

#include <unordered_map>
#include <set>
#include <memory>
#include "asio_poller.h"
#include "curl_class.h"

struct SocketData
{
    bool is_ares_socket = false;
    int previous_action = CURL_POLL_NONE; // what libcurl last asked for

    // What is actually pending on asio, which previous_action cannot express:
    // libcurl can move a socket INOUT->IN while the INOUT-armed write wait is
    // still outstanding. Deriving armed-ness from previous_action instead is
    // what dropped readiness events and stalled transfers to CURLOPT_TIMEOUT.
    bool read_armed = false;
    bool write_armed = false;
};

using SocketDataPtr = std::shared_ptr<SocketData>;

class CurlMulti
{
public:
    using CurlPerformComplete = std::function<void(CURLcode)>;

    CurlMulti(AsioPoller& asio_poller);
    ~CurlMulti();

    // Explicit teardown of the CURLM* so OnAmxxDetach can free the multi BEFORE
    // curl_global_cleanup() (libcurl requires all multi handles cleaned up first;
    // the destructor cascade otherwise runs curl_multi_cleanup after global
    // teardown = UB). Idempotent; ~CurlMulti also calls it.
    void Shutdown();

    // Returns the curl_multi_add_handle result so the caller can undo its
    // in-progress state on failure instead of leaking a permanent zombie handle.
    CURLMcode AddCurl(Curl& curl, CurlMulti::CurlPerformComplete&& callback);
    void RemoveCurl(Curl& curl);

    curl_socket_t CurlOpenSocketCallback(curlsocktype purpose, struct curl_sockaddr *address);
    int CurlCloseSocketCallback(curl_socket_t item);

    int CurlSocketCallback(CURL *easy, curl_socket_t s, int what, void *socketp);
    int CurlTimerCallback(CURLM *multi, long timeout_ms);

private:
    CurlMulti(const CurlMulti& curl_multi);
    void CheckMultiInfo();
    void SetSock(int act, curl_socket_t s, SocketDataPtr socketData);
    // Arms whichever direction libcurl wants and is not already pending. Takes the
    // socket rather than looking it up: both callers must already have proven the fd
    // is in socket_map_, so requiring the reference makes that a precondition the
    // compiler enforces instead of a second lookup and a guard that cannot fire.
    void ArmWaits(int act, curl_socket_t s, SocketDataPtr socketData,
                  asio::ip::tcp::socket& tcp_socket);
    // `dir` is ONE of CURL_POLL_IN / CURL_POLL_OUT — the direction this handler
    // was armed for, never the composite. A handler that cannot say which way it
    // fired cannot report the right readiness to libcurl.
    void AsioSocketActionCallback(int dir, curl_socket_t s, SocketDataPtr socketData, const asio::error_code& error);
    void AsioTimerCallback(const asio::error_code& error);

private:
    CURLM* curl_multi_;
    AsioPoller& asio_poller_;
    std::unordered_map<CURL*, CurlMulti::CurlPerformComplete> curl_map_;
    std::unordered_map<curl_socket_t, asio::ip::tcp::socket> socket_map_;
    std::unordered_map<curl_socket_t, SocketDataPtr> socket_data_map_;  // Prevents use-after-free
    std::set<curl_socket_t> removed_sockets_;
    int running_handles_;
};

#endif // _CURL_MULTI_CLASS_H_
