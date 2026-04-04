/* Force-included (-include) for test TUs before session.hpp: defines wsserver_t et al. */
#pragma once
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> wsserver_t;
typedef websocketpp::server<websocketpp::config::asio>::message_ptr message_ptr;
typedef websocketpp::server<websocketpp::config::asio>::connection_ptr connection_ptr;
typedef websocketpp::connection_hdl connection_hdl;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::steady_timer> timer_ptr;
